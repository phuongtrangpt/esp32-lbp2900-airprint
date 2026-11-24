// SPDX-License-Identifier: GPL-3.0-or-later
#include "hp_raw_bridge.h"
#include "hp_raw_xqx.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "status_led.h"
#include "usb_printer.h"
#include "wifi.h"

#define RAW_PORT 9100
#define RAW_IDLE_SECONDS 30
#define RAW_REPLY_POLL_MS 50
#define RAW_PREAMBLE_SECONDS 60
#define RAW_BUFFER_SIZE 4096

static const char *TAG = "hp_raw_bridge";

static ssize_t timed_recv(int fd, uint8_t *buffer, size_t size, int64_t *elapsed_us)
{
    int64_t start = esp_timer_get_time();
    ssize_t count = recv(fd, buffer, size, 0);
    int recv_errno = errno;
    *elapsed_us += esp_timer_get_time() - start;
    errno = recv_errno;
    return count;
}

static esp_err_t timed_usb_flush(int64_t *elapsed_us)
{
    int64_t start = esp_timer_get_time();
    esp_err_t result = usb_printer_raw_flush();
    *elapsed_us += esp_timer_get_time() - start;
    return result;
}

static esp_err_t relay_reply(int fd)
{
    uint8_t reply[512];
    int count = usb_printer_raw_poll_reply(reply, sizeof(reply));
    if (count < 0) {
        ESP_LOGW(TAG, "RAW printer reply unavailable");
        return ESP_FAIL;
    }
    for (int sent = 0; sent < count;) {
        ssize_t n = send(fd, reply + sent, (size_t)(count - sent), 0);
        if (n <= 0) {
            ESP_LOGW(TAG, "RAW reply send failed: errno=%d", errno);
            return ESP_FAIL;
        }
        sent += (int)n;
    }
    if (count)
        ESP_LOGI(TAG, "RAW USB reply relayed to Windows, %d bytes", count);
    return ESP_OK;
}

static void forward_job(int fd, uint8_t *buffer)
{
    struct timeval timeout = {.tv_sec = RAW_IDLE_SECONDS};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        ESP_LOGW(TAG, "Cannot set RAW receive timeout: errno=%d", errno);
        return;
    }

    ssize_t count = recv(fd, buffer, RAW_BUFFER_SIZE, 0);
    if (count <= 0)
        return;

    // HP's port monitor can wait for a USB status reply before sending XQX.
    // Poll that reply promptly instead of adding two one-second TCP waits.
    timeout.tv_sec = 0;
    timeout.tv_usec = RAW_REPLY_POLL_MS * 1000;
    struct timeval send_timeout = {.tv_sec = 1};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout)) != 0) {
        ESP_LOGW(TAG, "Cannot set RAW polling timeout: errno=%d", errno);
        return;
    }

    usb_printer_info_t printer;
    if (!usb_printer_get_info(&printer) || printer.vid != 0x03f0 || printer.pid != 0x3e17) {
        ESP_LOGW(TAG, "RAW rejected: HP P1006 not connected");
        return;
    }
    esp_err_t result = usb_printer_job_begin();
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "RAW printer unavailable: %s", esp_err_to_name(result));
        return;
    }

    status_led_set_raw_printing(true);

    size_t sent = 0;
    hp_raw_xqx_t xqx = {0};
    int64_t tcp_us = 0, usb_us = 0;
    int64_t page_tcp_us = 0, page_usb_us = 0;
    TickType_t job_start = xTaskGetTickCount();
    bool query_timeout = false;
    ESP_LOGI(TAG, "RAW job started, first read %d bytes", (int)count);
    for (;;) {
        if (xqx.state == XQX_PREAMBLE &&
            (TickType_t)(xTaskGetTickCount() - job_start) >= pdMS_TO_TICKS(RAW_PREAMBLE_SECONDS * 1000)) {
            ESP_LOGW(TAG, "RAW timed out waiting for XQX start after %zu bytes", sent);
            result = ESP_ERR_TIMEOUT;
            // A control-only PJL session has not started a print document.
            // Do not soft-reset HP merely because its monitor kept polling.
            query_timeout = !xqx.marker_bytes;
            break;
        }
        int64_t write_start = esp_timer_get_time();
        result = usb_printer_raw_write(buffer, (size_t)count);
        usb_us += esp_timer_get_time() - write_start;
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "RAW USB write failed after %zu bytes: %s",
                     sent, esp_err_to_name(result));
            break;
        }
        unsigned pages_before = xqx.pages_done;
        hp_raw_xqx_feed(&xqx, buffer, (size_t)count);
        sent += (size_t)count;
        if (xqx.pages_done != pages_before) {
            ESP_LOGI(TAG, "RAW XQX page %u boundary at %zu bytes: TCP recv %lld ms, USB write/flush %lld ms since previous boundary",
                     xqx.pages_done, sent, (long long)((tcp_us - page_tcp_us) / 1000),
                     (long long)((usb_us - page_usb_us) / 1000));
            page_tcp_us = tcp_us;
            page_usb_us = usb_us;
        }
        TickType_t idle_start = xTaskGetTickCount();
        bool drained = false;
        while ((count = timed_recv(fd, buffer, RAW_BUFFER_SIZE, &tcp_us)) < 0 &&
               (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!drained) {
                ESP_LOGI(TAG, "RAW TCP idle after %zu bytes; flushing pending USB", sent);
                result = timed_usb_flush(&usb_us);
                if (result != ESP_OK) {
                    ESP_LOGW(TAG, "RAW idle USB flush failed: %s", esp_err_to_name(result));
                    break;
                }
                drained = true;
            }
            result = relay_reply(fd);
            if (result != ESP_OK)
                break;
            if ((TickType_t)(xTaskGetTickCount() - idle_start) >= pdMS_TO_TICKS(RAW_IDLE_SECONDS * 1000)) {
                errno = ETIMEDOUT;
                break;
            }
        }
        if (result != ESP_OK)
            break;
        if (count < 0) {
            if (errno == ECONNRESET && hp_raw_xqx_complete(&xqx)) {
                ESP_LOGI(TAG, "RAW TCP reset after XQX end-document; flushing %zu bytes", sent);
                result = timed_usb_flush(&usb_us);
                break;
            }
            ESP_LOGW(TAG, "RAW upload stopped after %zu bytes: errno=%d", sent, errno);
            query_timeout = errno == ETIMEDOUT && xqx.state == XQX_PREAMBLE && !xqx.marker_bytes;
            result = ESP_FAIL;
            break;
        }
        if (!count) {
            result = timed_usb_flush(&usb_us);
            break;
        }
    }
    if (query_timeout)
        ESP_LOGI(TAG, "RAW status-only session expired; releasing without printer reset");
    usb_printer_job_end(result == ESP_OK || query_timeout);
    status_led_set_raw_printing(false);
    ESP_LOGI(TAG, "RAW USB output %s, %zu bytes submitted: %s",
             result == ESP_OK ? "flushed" : "aborted", sent, esp_err_to_name(result));
    ESP_LOGI(TAG, "RAW timing: TCP recv %lld ms, USB write/flush %lld ms",
             (long long)(tcp_us / 1000), (long long)(usb_us / 1000));
}

static void listen_task(void *arg)
{
    uint8_t *buffer = arg;
    for (;;) {
        char ip[16];
        if (!wifi_get_ip(ip, sizeof(ip))) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        struct sockaddr_in addr = {
            .sin_family = AF_INET, .sin_port = htons(RAW_PORT)
        };
        if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener >= 0) {
            const int reuse = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0 &&
                listen(listener, 1) == 0) {
                ESP_LOGI(TAG, "Listening on Wi-Fi station %s:%d", ip, RAW_PORT);
                for (;;) {
                    char current_ip[16];
                    if (!wifi_get_ip(current_ip, sizeof(current_ip)) ||
                        inet_addr(current_ip) != addr.sin_addr.s_addr)
                        break;
                    fd_set ready;
                    FD_ZERO(&ready);
                    FD_SET(listener, &ready);
                    struct timeval poll = {.tv_sec = 1};
                    int selected = select(listener + 1, &ready, NULL, NULL, &poll);
                    if (selected < 0)
                        break;
                    if (!selected)
                        continue;
                    int fd = accept(listener, NULL, NULL);
                    if (fd < 0)
                        break;
                    forward_job(fd, buffer);
                    close(fd);
                }
            } else {
                ESP_LOGW(TAG, "Cannot listen on %s:%d: errno=%d", ip, RAW_PORT, errno);
            }
            close(listener);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t hp_raw_bridge_start(void)
{
    // The listener reuses one receive buffer. Prefer PSRAM so idle RAW does
    // not take internal RAM needed by IPP and web connections.
    uint8_t *buffer = heap_caps_malloc(RAW_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGW(TAG, "RAW receive buffer falling back to internal RAM");
        buffer = heap_caps_malloc(RAW_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!buffer)
        return ESP_ERR_NO_MEM;
    if (xTaskCreate(listen_task, "hp_raw", 4096, buffer, 4, NULL) != pdPASS) {
        free(buffer);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
