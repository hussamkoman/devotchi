idf_component_register(
    SRCS "Power.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config esp_driver_gpio esp_pm esp_adc
    # Add esp_driver_ledc when implementing backlight PWM.
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
