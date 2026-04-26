idf_component_register(
    SRCS "Buttons.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config esp_driver_gpio button
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
