idf_component_register(
    SRCS "Feedback.cpp"
    INCLUDE_DIRS "include"
    REQUIRES config esp_driver_gpio esp_driver_ledc esp_timer
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
