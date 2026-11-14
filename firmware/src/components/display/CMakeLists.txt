idf_component_register(
    SRCS "Display.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config esp_lcd esp_driver_gpio esp_driver_spi esp_driver_ledc
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
