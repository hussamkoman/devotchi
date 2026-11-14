idf_component_register(
    SRCS "app.c" "app_cmds.c" "app_cmds_io.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES core
    PRIV_REQUIRES buttons debug_console display feedback power storage ui esp_timer esp_app_format esp_hw_support esp_system heap
)
