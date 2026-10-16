idf_component_register(SRCS "ble_console.c" "ble_frame.c" INCLUDE_DIRS "include"
    REQUIRES relay_control controller_runtime bt nvs_flash esp_timer esp_hw_support)
