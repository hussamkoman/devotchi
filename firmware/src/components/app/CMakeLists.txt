idf_component_register(
    SRCS "app.c" "app_cmds.c" "app_cmds_io.c" "app_cmds_net.c" "app_link.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES core
    PRIV_REQUIRES net buttons debug_console display feedback power storage ui esp_timer esp_app_format esp_hw_support esp_rom esp_system heap
)
