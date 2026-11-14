idf_component_register(
    SRCS "ui.c" "screen_splash.c" "screen_provisioning.c" "screen_wifi.c"
         "screen_connecting.c" "screen_pairing.c" "screen_pet.c" "pet_view.c" "screen_approval.c"
         "screen_identify.c" "status_bar.c" "overlay.c" "ui_font_mono_13.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES core
    PRIV_REQUIRES display lvgl esp_timer esp_app_format
)
