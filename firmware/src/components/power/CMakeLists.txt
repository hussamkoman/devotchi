idf_component_register(
    SRCS "Power.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config driver esp_pm esp_adc
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
