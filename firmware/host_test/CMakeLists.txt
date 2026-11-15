cmake_minimum_required(VERSION 3.22)

# An activated ESP-IDF shell can put cross binutils named plain `as`, `ld`
# and `ar` ahead of the host ones in PATH; gcc and CMake both resolve them
# through PATH. Pin the host build to the system binutils.
find_program(DEVOTCHI_HOST_AS NAMES as PATHS /usr/bin /usr/local/bin /bin NO_DEFAULT_PATH)
if(DEVOTCHI_HOST_AS)
    get_filename_component(DEVOTCHI_HOST_BINUTILS_DIR "${DEVOTCHI_HOST_AS}" DIRECTORY)
    set(CMAKE_C_FLAGS_INIT "-B${DEVOTCHI_HOST_BINUTILS_DIR}")
    find_program(CMAKE_AR NAMES ar PATHS "${DEVOTCHI_HOST_BINUTILS_DIR}" NO_DEFAULT_PATH)
    find_program(CMAKE_RANLIB NAMES ranlib PATHS "${DEVOTCHI_HOST_BINUTILS_DIR}" NO_DEFAULT_PATH)
endif()

project(devotchi_host_test C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(DEVOTCHI_HOST_TEST_FETCH "Download cJSON and Unity instead of using the ESP-IDF copies" OFF)

set(CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/components/core")
set(IDF_CJSON_DIR "${CMAKE_CURRENT_LIST_DIR}/../managed_components/espressif__cjson/cJSON")
set(IDF_UNITY_DIR "$ENV{IDF_PATH}/components/unity/unity/src")

if(NOT DEVOTCHI_HOST_TEST_FETCH
   AND EXISTS "${IDF_CJSON_DIR}/cJSON.c"
   AND EXISTS "${IDF_UNITY_DIR}/unity.c")
    set(CJSON_DIR "${IDF_CJSON_DIR}")
    set(UNITY_DIR "${IDF_UNITY_DIR}")
    message(STATUS "cJSON and Unity from ESP-IDF")
else()
    include(FetchContent)
    # SOURCE_SUBDIR points at a directory without a CMakeLists.txt so the
    # sources are downloaded but the upstream build files are not used.
    FetchContent_Declare(cjson
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON
        GIT_TAG v1.7.19
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR devotchi-sources-only)
    FetchContent_Declare(unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity
        GIT_TAG v2.6.1
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR devotchi-sources-only)
    FetchContent_MakeAvailable(cjson unity)
    set(CJSON_DIR "${cjson_SOURCE_DIR}")
    set(UNITY_DIR "${unity_SOURCE_DIR}/src")
    message(STATUS "cJSON and Unity from FetchContent")
endif()

# AddressSanitizer and UBSan need their runtime libraries (libasan, libubsan).
# Where those are not installed the tests still run, with UBSan in trap mode,
# which needs no runtime.
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
check_c_source_compiles("int main(void) { return 0; }" DEVOTCHI_HAVE_SANITIZER_RUNTIME)
unset(CMAKE_REQUIRED_FLAGS)
if(DEVOTCHI_HAVE_SANITIZER_RUNTIME)
    set(SANITIZE -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
else()
    message(WARNING "libasan/libubsan not found: building without AddressSanitizer, UBSan in trap mode")
    set(SANITIZE -fsanitize=undefined -fsanitize-trap=undefined)
endif()

# Third-party code is built without -Werror.
add_library(third_party STATIC "${CJSON_DIR}/cJSON.c" "${UNITY_DIR}/unity.c")
target_include_directories(third_party SYSTEM PUBLIC "${CJSON_DIR}" "${UNITY_DIR}")
target_compile_definitions(third_party PUBLIC CJSON_NESTING_LIMIT=1000)
target_compile_options(third_party PRIVATE -g ${SANITIZE})

file(GLOB CORE_SRCS CONFIGURE_DEPENDS "${CORE_DIR}/*.c")
add_library(core STATIC ${CORE_SRCS})
target_include_directories(core PUBLIC "${CORE_DIR}/include")
target_compile_options(core PRIVATE -Wall -Wextra -Werror -g ${SANITIZE})
target_link_libraries(core PUBLIC third_party)

enable_testing()

# One executable and one ctest entry per host_test/test_<module>.c.
file(GLOB TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/test_*.c")
foreach(test_src ${TEST_SRCS})
    get_filename_component(test_name "${test_src}" NAME_WE)
    add_executable(${test_name} "${test_src}")
    target_compile_options(${test_name} PRIVATE -Wall -Wextra -Werror -g ${SANITIZE})
    target_link_options(${test_name} PRIVATE ${SANITIZE})
    target_link_libraries(${test_name} PRIVATE core)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# The golden packs the compiled-in arrays are compared with.
target_compile_definitions(test_pet_pack PRIVATE PETS_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/../../pets/golden")
