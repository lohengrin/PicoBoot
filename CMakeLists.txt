cmake_minimum_required(VERSION 3.20)

set(TOOLSET_ROOT ${CMAKE_CURRENT_LIST_DIR}/third_party/pico-toolset)

# Phase 1 target board: Waveshare RP2350-PiZero (Pico-Toolset "Implemented"
# status, validated sdcard/PSRAM/USB-HID presets). Later phases parameterize
# this for RP2040 boards (see docs/architecture.md, Phase 8).
set(PICO_BOARD waveshare_rp2350_pizero CACHE STRING "")
list(APPEND PICO_BOARD_HEADER_DIRS ${TOOLSET_ROOT}/boards)
set(PICO_PLATFORM rp2350 CACHE STRING "" FORCE)

if(NOT PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH} CACHE PATH "" FORCE)
    else()
        message(FATAL_ERROR "PICO_SDK_PATH must be set (env or -D) to build PicoBoot.")
    endif()
endif()
include(${TOOLSET_ROOT}/pico_sdk_import.cmake)

project(picoboot C CXX ASM)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
pico_sdk_init()

# Pull in only the Pico-Toolset components each phase needs -- following
# examples/waveshare_pizero/CMakeLists.txt's cherry-pick pattern rather than
# add_subdirectory()'ing the whole toolset (which would build every
# component, including ones PicoBoot doesn't use).
add_subdirectory(${TOOLSET_ROOT}/components/reset_buttons ${CMAKE_BINARY_DIR}/_toolset/reset_buttons)

add_subdirectory(boot_core)
add_subdirectory(targets/picoboot_serial)
