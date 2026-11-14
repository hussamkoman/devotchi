idf_component_register(
    SRCS "ui.c" "screen_splash.c" "screen_diag.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES core display lvgl esp_timer esp_app_format
)
