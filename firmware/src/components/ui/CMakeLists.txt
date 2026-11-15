idf_component_register(
    SRCS "ui.c" "ui_theme.c" "pet_view.c"
         "screen_splash.c" "screen_provisioning.c" "screen_wifi.c" "screen_connecting.c" "screen_pairing.c"
         "screen_pet.c" "screen_approval.c" "screen_identify.c" "screen_settings.c" "screen_lowbatt.c"
         "widgets/w_status.c" "widgets/w_tabs.c" "widgets/w_toast.c" "widgets/w_list.c" "widgets/w_well.c"
         "widgets/w_countdown.c" "widgets/w_chip.c"
         "fonts/ui_font_display64.c" "fonts/ui_font_title24.c" "fonts/ui_font_body18.c"
         "fonts/ui_font_small14.c" "fonts/ui_font_mono14.c" "fonts/ui_font_icons.c" "fonts/ui_img_lockup.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES core
    PRIV_REQUIRES display lvgl esp_timer esp_app_format esp_hw_support
)
