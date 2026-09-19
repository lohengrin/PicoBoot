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
# Bootloader flash footprint is a hard constraint (see cmake/picoboot_flash_layout.cmake):
# default to size-optimized unless the user picks a build type.
set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build type")
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

# SD card (Phase 2): PICO_TOOLSET_BUILD_SDCARD gates cmake/pico_fatfs.cmake
# below (it needs the option ON before it runs). PICO_TOOLSET_SDCARD_STDIO
# stays OFF here (see config/CMakeLists.txt for why) -- config::PicoBootConfig
# compiles the stdio shim source directly into its own target instead.
set(PICO_TOOLSET_BUILD_SDCARD ON)
include(${TOOLSET_ROOT}/cmake/pico_fatfs.cmake)
add_subdirectory(${TOOLSET_ROOT}/components/sdcard ${CMAKE_BINARY_DIR}/_toolset/sdcard)

add_subdirectory(${TOOLSET_ROOT}/components/usb_composite ${CMAKE_BINARY_DIR}/_toolset/usb_composite)

# LVGL LCD UI (Phase 6): ILI9486 panel + XPT2046 touch + LVGL bridge.
add_subdirectory(${TOOLSET_ROOT}/components/driver_interfaces ${CMAKE_BINARY_DIR}/_toolset/driver_interfaces)
add_subdirectory(${TOOLSET_ROOT}/components/ili9486 ${CMAKE_BINARY_DIR}/_toolset/ili9486)
add_subdirectory(${TOOLSET_ROOT}/components/xpt2046 ${CMAKE_BINARY_DIR}/_toolset/xpt2046)
set(LV_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/ui/lvgl/lv_conf.h)
include(${TOOLSET_ROOT}/cmake/pico_lvgl.cmake)
add_subdirectory(${TOOLSET_ROOT}/components/dvi_hdmi ${CMAKE_BINARY_DIR}/_toolset/dvi_hdmi)
add_subdirectory(${TOOLSET_ROOT}/components/lvgl_display ${CMAKE_BINARY_DIR}/_toolset/lvgl_display)

add_subdirectory(boot_core)
add_subdirectory(storage)
add_subdirectory(config)
add_subdirectory(app_manager)
add_subdirectory(usb)
add_subdirectory(ui)
add_subdirectory(targets/picoboot_serial)
add_subdirectory(targets/picoboot_lvgl_lcd)
add_subdirectory(targets/picoboot_lvgl_dvi)
