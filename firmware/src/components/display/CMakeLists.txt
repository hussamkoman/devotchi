idf_component_register(
    SRCS "Display.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config esp_lcd driver
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
