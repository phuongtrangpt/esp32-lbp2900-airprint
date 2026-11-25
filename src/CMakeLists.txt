set(HP_EXTRA_SOURCES "")
if(PRINTSERVER_PROFILE STREQUAL "hp_pc" OR PRINTSERVER_PROFILE STREQUAL "hp" OR PRINTSERVER_PROFILE STREQUAL "hp_p1008" OR PRINTSERVER_PROFILE STREQUAL "all")
    execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../tools/prepare_hp_firmware.py"
                    --out "${CMAKE_CURRENT_LIST_DIR}/hp_p1006.dl"
                    RESULT_VARIABLE HP_PREPARE_RESULT)
    if(NOT HP_PREPARE_RESULT EQUAL 0)
        message(FATAL_ERROR "Cannot prepare verified HP P1006 embedded firmware")
    endif()
    list(APPEND HP_EXTRA_SOURCES "hp_p1006_blob.c")
endif()
if(PRINTSERVER_PROFILE STREQUAL "hp" AND PRINTSERVER_HP_RAW_BRIDGE)
    list(APPEND HP_EXTRA_SOURCES "hp_raw_bridge.c")
endif()
if(PRINTSERVER_PROFILE STREQUAL "hp_pc")
    list(APPEND HP_EXTRA_SOURCES "hp_pc_bridge.c")
endif()
idf_component_register(SRCS "main.c" "status_led.c" "usb_printer.c" "hp_firmware.c" "print_job.c" "wifi.c" "setup_portal.c" "cloud_update.c" "network_settings.c" "airprint_esp.c" "debug.c" "symtab.c" "oled.c" "qemu_selftest.c" ${HP_EXTRA_SOURCES}
                       INCLUDE_DIRS "."
                       REQUIRES hp_offload portal_dns esp_http_client mbedtls hp_bootstrap network_config json improv_serial esp_driver_uart usb canon_bjl airprint printers scanner webui oled esp_driver_rmt esp_driver_gpio esp_driver_i2c esp_timer esp_wifi
                                esp_netif esp_eth nvs_flash mdns
                                esp_http_server app_update espcoredump esp_partition bootloader_support)
