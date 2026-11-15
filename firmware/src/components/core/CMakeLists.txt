idf_component_register(
    SRCS "card_fsm.c" "text.c" "cmdline.c" "btn_fsm.c" "fb_seq.c" "battery.c" "backoff.c" "url.c" "pair_code.c" "ws_reasm.c"
         "proto.c" "reqq.c" "pet_model.c" "pet_pose.c" "settings_menu.c" "power_fsm.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES espressif__cjson
)
