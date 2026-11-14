idf_component_register(
    SRCS "app.c" "app_cmds.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES debug_console core display storage ui esp_timer esp_app_format esp_hw_support esp_system heap
)
