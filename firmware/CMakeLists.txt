cmake_minimum_required(VERSION 3.22)

# EXTRA_COMPONENT_DIRS takes a component directory (src/main) as well as a
# directory of components (src/components).
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/src/main"
    "${CMAKE_CURRENT_LIST_DIR}/src/components"
)

set(PROJECT_VER "0.2.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(devotchi)
