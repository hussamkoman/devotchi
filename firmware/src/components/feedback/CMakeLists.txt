idf_component_register(
    SRCS "feedback.c"
    INCLUDE_DIRS "include"
    REQUIRES core
    PRIV_REQUIRES board storage esp_driver_ledc esp_timer log
)
