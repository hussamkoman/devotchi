idf_component_register(
    SRCS "card_fsm.c" "text.c" "cmdline.c" "btn_fsm.c" "fb_seq.c" "battery.c" "backoff.c" "url.c" "pair_code.c" "ws_reasm.c"
         "proto.c" "qstore.c" "reqq.c" "pet_model.c" "pet_pack.c" "pet_pose.c" "pet_builtin.c" "pet_install.c" "settings_menu.c" "power_fsm.c" "clock.c" "showcase.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES espressif__cjson
)

include("${CMAKE_CURRENT_LIST_DIR}/../../../tools/pet_builtins.cmake")
idf_build_get_property(python PYTHON)
devotchi_pet_builtins("${python}" "${CMAKE_CURRENT_BINARY_DIR}/pets")
target_sources(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/pets/pet_builtins.c")
target_include_directories(${COMPONENT_LIB} PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/pets")
