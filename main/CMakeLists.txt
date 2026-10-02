# The C6 firmware is embedded in the application, put it in firmware/network_adapter.bin (see README.md)
set(C6_FIRMWARE "${CMAKE_CURRENT_SOURCE_DIR}/../firmware/network_adapter.bin")
if(NOT EXISTS "${C6_FIRMWARE}")
    message(FATAL_ERROR "Missing ${C6_FIRMWARE}\n"
        "Copy the ESP32-C6 ESP-Hosted firmware (network_adapter.bin) there, README.md explains how to get one.")
endif()

idf_component_register(SRCS "main.c" "tab5_power.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_hosted nvs_flash esp_event esp_timer esp_driver_i2c
                       EMBED_FILES "${C6_FIRMWARE}")
