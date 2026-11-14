idf_component_register(
    SRCS "Power.cpp" "PowerTask.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config display runtime esp_driver_gpio esp_pm esp_adc
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
