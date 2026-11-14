idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES app debug_console display storage ui espressif__cjson heap
)
