# ESP-IDF project file. Code lives under src/ to keep the root clean
# (build/, sdkconfig, partitions, defaults stay at the root).
cmake_minimum_required(VERSION 3.22)

# EXTRA_COMPONENT_DIRS accepts both "a directory containing components"
# and "a component directly" — so we list main as itself and components as a parent.
list(APPEND EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/src/main"
    "${CMAKE_CURRENT_LIST_DIR}/src/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(devotchi)
