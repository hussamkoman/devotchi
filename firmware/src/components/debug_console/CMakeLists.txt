idf_component_register(
    SRCS "console.c"
    INCLUDE_DIRS "include"
    REQUIRES espressif__cjson
    PRIV_REQUIRES core esp_driver_usb_serial_jtag esp_app_format log esp_system
)
