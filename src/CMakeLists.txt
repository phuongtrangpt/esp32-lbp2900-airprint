set(HP_EMBED_FILES "")
if(NOT PRINTSERVER_PROFILE STREQUAL "canon")
    execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../tools/prepare_hp_firmware.py"
                    --out "${CMAKE_CURRENT_LIST_DIR}/hp_p1006.dl"
                    RESULT_VARIABLE HP_PREPARE_RESULT)
    if(NOT HP_PREPARE_RESULT EQUAL 0)
        message(FATAL_ERROR "Cannot prepare verified HP P1006 embedded firmware")
    endif()
    list(APPEND HP_EMBED_FILES "hp_p1006.dl")
endif()
idf_component_register(SRCS "main.c" "status_led.c" "usb_printer.c" "hp_firmware.c" "print_job.c" "wifi.c" "setup_portal.c" "cloud_update.c" "network_settings.c" "airprint_esp.c" "debug.c" "symtab.c" "oled.c" "qemu_selftest.c"
                       EMBED_FILES ${HP_EMBED_FILES}
                       INCLUDE_DIRS "."
                       REQUIRES portal_dns esp_http_client mbedtls hp_bootstrap network_config json improv_serial esp_driver_uart usb canon_bjl airprint printers scanner webui oled esp_driver_rmt esp_driver_gpio esp_driver_i2c esp_timer esp_wifi
                                esp_netif esp_eth nvs_flash mdns
                                esp_http_server app_update espcoredump esp_partition bootloader_support)
