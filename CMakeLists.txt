cmake_minimum_required(VERSION 3.20)

set(TOOLSET_ROOT ${CMAKE_CURRENT_LIST_DIR}/third_party/pico-toolset)

# Board selection. One build directory per board:
#   cmake -S . -B build                                   # waveshare_pizero (default)
#   cmake -S . -B build-crowpanel -DPICOBOOT_BOARD=crowpanel_pico_hmi_28
#   cmake -S . -B build-picodv    -DPICOBOOT_BOARD=pico_dv
set(PICOBOOT_BOARD "waveshare_pizero" CACHE STRING "Board: waveshare_pizero | crowpanel_pico_hmi_28 | pico_dv")
set_property(CACHE PICOBOOT_BOARD PROPERTY STRINGS waveshare_pizero crowpanel_pico_hmi_28 pico_dv)
list(APPEND PICO_BOARD_HEADER_DIRS ${TOOLSET_ROOT}/boards)
if(PICOBOOT_BOARD STREQUAL "waveshare_pizero")
    # RP2350, onboard HDMI + uSD + PIO-USB host; optional external 3.5" LCD.
    set(PICO_BOARD waveshare_rp2350_pizero CACHE STRING "" FORCE)
    set(PICO_PLATFORM rp2350 CACHE STRING "" FORCE)
elseif(PICOBOOT_BOARD STREQUAL "crowpanel_pico_hmi_28")
    # RP2040 (Pico form factor): 320x240 ST7789 + XPT2046 touch + uSD, no USB host.
    set(PICO_BOARD pico CACHE STRING "" FORCE)
    set(PICO_PLATFORM rp2040 CACHE STRING "" FORCE)
elseif(PICOBOOT_BOARD STREQUAL "pico_dv")
    # RP2040 Pico W on the "Pico DV" carrier: HDMI/DVI + uSD + 3 buttons.
    set(PICO_BOARD pico_w CACHE STRING "" FORCE)
    set(PICO_PLATFORM rp2040 CACHE STRING "" FORCE)
else()
    message(FATAL_ERROR "Unknown PICOBOOT_BOARD '${PICOBOOT_BOARD}'")
endif()
string(TOUPPER ${PICOBOOT_BOARD} PICOBOOT_BOARD_UPPER)

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

# Selects the per-board presets in targets/common/board.h.
add_compile_definitions(PICOBOOT_BOARD_${PICOBOOT_BOARD_UPPER}=1)

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

# USB HID host (PIO-USB keyboard/mouse): only where the board has the port
# and a free PIO (Waveshare RP2350-PiZero). Must be added *before*
# usb_composite so it can define its "_hid" variant.
if(PICOBOOT_BOARD STREQUAL "waveshare_pizero")
    set(PICO_TOOLSET_BUILD_USB_HID ON)
    set(PICO_TOOLSET_USB_HID_KEYMAPS us CACHE STRING "" FORCE) # keep flash small
    set(PICO_TOOLSET_USB_HID_DEFAULT_KEYMAP us CACHE STRING "" FORCE)
    include(${TOOLSET_ROOT}/cmake/pico_pio_usb.cmake)
    add_subdirectory(${TOOLSET_ROOT}/components/usb_hid ${CMAKE_BINARY_DIR}/_toolset/usb_hid)
endif()

add_subdirectory(${TOOLSET_ROOT}/components/usb_composite ${CMAKE_BINARY_DIR}/_toolset/usb_composite)

# LVGL UI: panel/touch/video drivers per board + the LVGL bridge.
add_subdirectory(${TOOLSET_ROOT}/components/driver_interfaces ${CMAKE_BINARY_DIR}/_toolset/driver_interfaces)
if(PICOBOOT_BOARD STREQUAL "waveshare_pizero")
    add_subdirectory(${TOOLSET_ROOT}/components/ili9486 ${CMAKE_BINARY_DIR}/_toolset/ili9486)
endif()
if(PICOBOOT_BOARD STREQUAL "crowpanel_pico_hmi_28")
    add_subdirectory(${TOOLSET_ROOT}/components/st7789 ${CMAKE_BINARY_DIR}/_toolset/st7789)
endif()
if(NOT PICOBOOT_BOARD STREQUAL "pico_dv")
    add_subdirectory(${TOOLSET_ROOT}/components/xpt2046 ${CMAKE_BINARY_DIR}/_toolset/xpt2046)
endif()
if(NOT PICOBOOT_BOARD STREQUAL "crowpanel_pico_hmi_28")
    add_subdirectory(${TOOLSET_ROOT}/components/dvi_hdmi ${CMAKE_BINARY_DIR}/_toolset/dvi_hdmi)
endif()
set(LV_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/ui/lvgl/lv_conf.h)
include(${TOOLSET_ROOT}/cmake/pico_lvgl.cmake)
add_subdirectory(${TOOLSET_ROOT}/components/lvgl_display ${CMAKE_BINARY_DIR}/_toolset/lvgl_display)

add_subdirectory(boot_core)
add_subdirectory(storage)
add_subdirectory(config)
add_subdirectory(app_manager)
add_subdirectory(usb)
add_subdirectory(ui)
add_subdirectory(targets/picoboot_serial)
if(PICOBOOT_BOARD STREQUAL "waveshare_pizero")
    add_subdirectory(targets/picoboot_lvgl_lcd)
    add_subdirectory(targets/picoboot_lvgl_dvi)
elseif(PICOBOOT_BOARD STREQUAL "crowpanel_pico_hmi_28")
    add_subdirectory(targets/picoboot_lvgl_crowpanel)
endif()
