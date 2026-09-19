# Negative test: a normal pico-sdk app linked at the DEFAULT 0x10000000 (no partition offset).
# PicoBoot must reject it ("not built for this partition") instead of flashing it.
# Original description follows.
# Minimal test app: blinks the board LED then halts. Standalone pico-sdk
# firmware (no toolset dependency, deliberately, to keep this app's own
# build as simple as possible as a control case) -- only its FLASH origin
# is non-standard, via picoboot_set_app_flash_region() below. Proves the
# bare "jump into an app built at the offset origin and have it actually
# run" path (Phase 3), without conflating it with any other new code.
cmake_minimum_required(VERSION 3.20)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_testapp_board.cmake)
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../third_party/pico-toolset/boards)

if(NOT PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH} CACHE PATH "" FORCE)
    else()
        message(FATAL_ERROR "PICO_SDK_PATH must be set (env or -D) to build this test app.")
    endif()
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(app_wrong_offset C CXX ASM)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
pico_sdk_init()

add_executable(app_wrong_offset main.cpp)
target_link_libraries(app_wrong_offset PRIVATE pico_stdlib)

target_compile_definitions(app_wrong_offset PRIVATE PICOBOOT_TEST_LED_PIN=${PICOBOOT_TEST_LED_PIN})
pico_add_extra_outputs(app_wrong_offset)
