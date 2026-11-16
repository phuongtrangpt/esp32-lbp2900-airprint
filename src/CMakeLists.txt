idf_component_register(SRCS "main.c" "status_led.c" "usb_printer.c" "hp_firmware.c" "print_job.c" "wifi.c" "network_settings.c" "airprint_esp.c" "debug.c" "symtab.c" "oled.c" "qemu_selftest.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_http_client mbedtls hp_bootstrap network_config json improv_serial esp_driver_uart usb canon_bjl airprint printers scanner webui oled esp_driver_rmt esp_driver_gpio esp_driver_i2c esp_timer esp_wifi
                                esp_netif esp_eth nvs_flash mdns
                                esp_http_server app_update espcoredump esp_partition bootloader_support)
