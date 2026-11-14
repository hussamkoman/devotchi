idf_component_register(
    SRCS "power.c"
    INCLUDE_DIRS "include"
    REQUIRES core
    PRIV_REQUIRES board storage esp_adc esp_driver_gpio esp_rom esp_timer log
)
