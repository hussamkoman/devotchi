cmake_minimum_required(VERSION 3.22)

# EXTRA_COMPONENT_DIRS takes a component directory (src/main) as well as a
# directory of components (src/components).
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/src/main"
    "${CMAKE_CURRENT_LIST_DIR}/src/components"
)

set(PROJECT_VER "0.2.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# LVGL's default font is the theme's small font (ui/fonts/ui_font_small14.c);
# its Kconfig can only name a built-in font.
idf_build_set_property(COMPILE_DEFINITIONS "LV_FONT_CUSTOM_DECLARE=LV_FONT_DECLARE(ui_font_small14)" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "LV_FONT_DEFAULT=&ui_font_small14" APPEND)
# LVGL's heap (objects, styles, label texts, its own draw layers) lives in
# PSRAM, which keeps internal RAM for WiFi, stacks and the DMA draw buffers:
# with CONFIG_SPIRAM_USE_MALLOC, malloc() serves a block above
# CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (16 kB) from PSRAM. LVGL calls
# LV_MEM_POOL_ALLOC(LV_MEM_SIZE) once, from lv_init().
idf_build_set_property(COMPILE_DEFINITIONS "LV_MEM_POOL_INCLUDE=<stdlib.h>" APPEND)
idf_build_set_property(COMPILE_DEFINITIONS "LV_MEM_POOL_ALLOC=malloc" APPEND)

project(devotchi)

# The image must fit a later OTA slot of 0x1F0000 bytes with 64 kB to spare
# (architecture 4.11, 7.3): every build fails above this size.
set(DEVOTCHI_APP_MAX_BYTES 1966080)
add_custom_target(check_app_size ALL
    COMMAND ${CMAKE_COMMAND} -DBIN=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin -DMAX=${DEVOTCHI_APP_MAX_BYTES}
            -P ${CMAKE_CURRENT_LIST_DIR}/tools/check_app_size.cmake
    DEPENDS gen_project_binary
    VERBATIM)
