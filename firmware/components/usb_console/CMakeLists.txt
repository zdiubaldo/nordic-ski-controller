idf_component_register(SRCS "usb_console.c" INCLUDE_DIRS "include"
    REQUIRES ble_console controller_runtime relay_control esp_timer esp_driver_usb_serial_jtag esp_hw_support)
