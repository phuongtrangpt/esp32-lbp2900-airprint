#ifdef PRINTSERVER_HP_RAW_BRIDGE
#include "hp_raw_bridge.h"
#endif
#include "network_settings.h"
#include "cloud_update.h"
// ESP32-S3 print server for the Canon PIXMA MG2500 series.
//
// LED: solid yellow = ready, no printer / solid green = printer attached /
//      blinking green = printing (test page or AirPrint) or scanning / fast red = last
//      job failed / blue flashes = Identify-Printer.
// BOOT button (and a second one on GPIO4, the same): press = scan an A4 page
//      at 150 dpi (kept on the device: every open web page shows it and saves
//      it to its Files), double press = print an A4 colour test page (dry run
//      without a printer), hold 2 s while printing or scanning = cancel. While
//      the screen is off, a press only turns it on.
// User button (GPIO5): wired and debounced, but does nothing yet: a free
//      button for your own feature (on_user_button below).
// AirPrint: joins the WiFi from .env and serves IPP on port 631; the web UI
//      is at http://<host>/ (port 80, and 631).
// Screen: an optional I2C OLED shows the IP address, the printer and what it
//      is doing (oled.h); it sleeps when nothing changes, a press wakes it.
// MODE=debug: also the remote log, crash dumps and firmware updates over
//      WiFi (debug.h, tools/remote.py).
#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "airprint_esp.h"
#include "airprint.h"
#include "debug.h"
#include "local_config.h"
#include "oled.h"
#include "print_job.h"
#include "scan_service.h"
#include "sdkconfig.h"
#include "status_led.h"
#include "usb_printer.h"
#include "wifi.h"

static const char *TAG = "main";

#define LONG_PRESS_MS 2000
#define DOUBLE_PRESS_MS 400     // a second press within this is a double press
#define ERROR_SHOW_MS 3000

static volatile bool s_busy;            // test page task running
static volatile bool s_airprint_busy;   // AirPrint job running
static volatile bool s_showing_error;

static void show_idle_state(void)
{
    bool recovery = airprint_printer_restart_required() || usb_printer_needs_reconnect();
    status_led_set(status_led_select(recovery, s_showing_error,
                                    s_busy || s_airprint_busy, usb_printer_connected()));
}

static void on_printer(bool connected, const usb_printer_info_t *info)
{
    if (connected) {
        airprint_printer_reconnected();
        ESP_LOGI(TAG, "printer ready: %s %s", info->manufacturer, info->product);
        if (print_job_set_printer(info))
            airprint_esp_driver_changed();     // another model: announce it
        scan_service_recalibrate();     // maybe powered off and on: calibrate again
    }
    show_idle_state();
}

static void on_airprint_job(bool printing)
{
    s_airprint_busy = printing;
    show_idle_state();
}

static void print_task(void *arg)
{
    bool dry_run = (bool)(intptr_t)arg;
    print_stats_t st;
    show_idle_state();
    debug_memory("test-page begin");
    esp_err_t err = print_job_test_page(dry_run, print_job_default_dpi(), &st);
    if (dry_run)
        ESP_LOGI(TAG, "dry run done: %lu bytes, crc32 %08lx, %lu ms (%s)", (unsigned long)st.bytes,
                 (unsigned long)st.crc32, (unsigned long)st.ms, esp_err_to_name(err));
    else
        ESP_LOGI(TAG, "print job finished: %s, %lu bytes in %lu ms", esp_err_to_name(err), (unsigned long)st.bytes,
                 (unsigned long)st.ms);
    debug_memory("test-page end");
    s_busy = false;
    if (err != ESP_OK) {
        s_showing_error = true;
        status_led_set(LED_ERROR);
        vTaskDelay(pdMS_TO_TICKS(ERROR_SHOW_MS));
        s_showing_error = false;
    }
    show_idle_state();
    vTaskDelete(NULL);
}

static void on_double_press(void);

static void on_press(void)
{
    if (s_busy || s_airprint_busy) {
        ESP_LOGI(TAG, "busy; hold BOOT for 2 s to cancel");
        return;
    }
    if (!print_job_driver()->scanner) { on_double_press(); return; }
    const char *why;
    if (scan_service_start_button(&why) == 0) {
        ESP_LOGI(TAG, "button: scanning; the web pages receive the scan");
        return;
    }
    ESP_LOGW(TAG, "button: cannot scan: %s", why);
    s_showing_error = true;
    status_led_set(LED_ERROR);
    vTaskDelay(pdMS_TO_TICKS(ERROR_SHOW_MS / 2));
    s_showing_error = false;
    show_idle_state();
}

static void on_double_press(void)
{
    if (s_busy || s_airprint_busy) {
        ESP_LOGI(TAG, "busy; hold BOOT for 2 s to cancel");
        return;
    }
    bool dry_run = !usb_printer_connected();
    if (dry_run)
        ESP_LOGI(TAG, "no printer attached: dry run (generate the job, discard it, log its CRC)");
    s_busy = true;
    // lower priority than the USB tasks; rendering is CPU heavy
    if (xTaskCreatePinnedToCore(print_task, "print", 8192, (void *)(intptr_t)dry_run, 4, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "cannot start print task");
        s_busy = false;
    }
}

// The second button (GPIO5): free for whatever you want it to do. Called once
// per press (debounced), on the button task: start long work in a task of its
// own. The press has already turned the screen on.
static void on_user_button(void)
{
    ESP_LOGI(TAG, "user button (GPIO%d) pressed: no action assigned", CONFIG_USER_BUTTON_GPIO);
}

static void input_pullup(int pin)
{
    if (pin < 0)
        return;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
}

static bool is_down(int pin)
{
    return pin >= 0 && gpio_get_level(pin) == 0;   // active low
}

// 30 ms of the same level; true when the stable state changes
static bool debounce(bool down, bool *stable, int *same)
{
    if (down == *stable) {
        *same = 0;
        return false;
    }
    if (++*same < 3)
        return false;
    *stable = down;
    *same = 0;
    return true;
}

static void button_task(void *arg)
{
    input_pullup(CONFIG_PRINT_BUTTON_GPIO);
    input_pullup(CONFIG_PRINT_BUTTON2_GPIO);
    input_pullup(CONFIG_USER_BUTTON_GPIO);

    // the print buttons (BOOT and its twin): one button for the logic below
    bool down = false;
    int same = 0;
    TickType_t pressed_at = 0, released_at = 0;
    bool long_fired = false, maintenance_fired = false;
    bool pending = false;       // a press waiting to see whether a second one follows
    bool waking = false;        // this press turned the screen on: it does nothing else
    // the user button
    bool user_down = false;
    int user_same = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        // Read atomic fault state even with no browser or status requests.
        show_idle_state();
        if (debounce(is_down(CONFIG_PRINT_BUTTON_GPIO) || is_down(CONFIG_PRINT_BUTTON2_GPIO), &down, &same)) {
            if (down) {
                pressed_at = xTaskGetTickCount();
                long_fired = false;maintenance_fired=false;
                waking = oled_wake();
                if (waking)
                    pending = false;
            } else if (!long_fired && !waking) {
                if (pending) {
                    pending = false;
                    on_double_press();
                } else {
                    pending = true;
                    released_at = xTaskGetTickCount();
                }
            }
        }
        if (pending && !down && xTaskGetTickCount() - released_at >= pdMS_TO_TICKS(DOUBLE_PRESS_MS)) {
            pending = false;
            on_press();
        }
        if (down && !long_fired && !waking && xTaskGetTickCount() - pressed_at >= pdMS_TO_TICKS(LONG_PRESS_MS)) {
            long_fired = true;
            if (s_busy || s_airprint_busy) {
                ESP_LOGW(TAG, "cancel requested");
                usb_printer_cancel();
                scan_service_cancel();
            }
        }

        if(down && !maintenance_fired && xTaskGetTickCount()-pressed_at>=pdMS_TO_TICKS(5000)) {
            maintenance_fired=true;cloud_update_arm();
        }
        if(down && xTaskGetTickCount()-pressed_at>=pdMS_TO_TICKS(10000) && !usb_printer_job_active()) {
            ESP_LOGW(TAG,"BOOT held 10 s: restoring DHCP (keeping Wi-Fi credentials)");
            if(!cloud_update_busy() && network_settings_reset()==ESP_OK && wifi_setup_next_boot()==ESP_OK) { vTaskDelay(pdMS_TO_TICKS(200)); esp_restart(); }
            pressed_at=xTaskGetTickCount();
        }
        if (debounce(is_down(CONFIG_USER_BUTTON_GPIO), &user_down, &user_same) && user_down && !oled_wake())
            on_user_button();
    }
}

static volatile bool s_debug_server_up;

static bool device_busy(void)
{
    return usb_printer_job_active() || cloud_update_busy();
}

// a freshly updated firmware is kept once the WiFi (and in debug mode the
// debug server, the way to fix things) has worked for a while
static bool healthy(void)
{
    return wifi_connected() && (strcmp(MODE, "debug") != 0 || s_debug_server_up);
}

static void ota_confirm_task(void *arg)
{
    debug_ota_confirm(healthy, 20, 180);
    vTaskDelete(NULL);
}

#ifdef PRINTSERVER_QEMU_SELFTEST
// app_main is in qemu_selftest.c
void main_unused(void)
{
    (void)button_task;
    (void)on_printer;
    (void)on_airprint_job;
    (void)ota_confirm_task;
    (void)device_busy;
}
#else
void app_main(void)
{
    bool debug = strcmp(MODE, "debug") == 0;
    debug_log_start(64 * 1024); // always capture, including MODE=normal
    ESP_ERROR_CHECK(status_led_init());
    status_led_set(LED_WAITING);
    oled_start(HOSTNAME);
    ESP_ERROR_CHECK(usb_printer_start(on_printer));
    xTaskCreate(button_task, "button", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "ready: BOOT (or GPIO%d) = test page on mono models; hold 2 s = cancel; hold 10 s = DHCP", CONFIG_PRINT_BUTTON2_GPIO);

    xTaskCreate(ota_confirm_task, "ota_confirm", 3072, NULL, 2, NULL);
    // AirPrint; the printer keeps working locally if WiFi isn't configured
    if (wifi_start(HOSTNAME) == ESP_OK) {
        setup_dns_start();
        if (airprint_esp_start(HOSTNAME, on_airprint_job) != ESP_OK)
            ESP_LOGE(TAG, "AirPrint failed to start");
#ifdef PRINTSERVER_HP_RAW_BRIDGE
        if (hp_raw_bridge_start() != ESP_OK)
            ESP_LOGE(TAG, "HP RAW bridge failed to start");
#endif
        if (debug)
            s_debug_server_up = debug_server_start(device_busy) == ESP_OK;
    }
}
#endif
