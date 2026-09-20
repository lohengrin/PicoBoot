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
add_subdirectory(${TOOLSET_ROOT}/components/reset_buttons ${CMAKE_BINARY_DIR}/_toolset/reset_buttons EXCLUDE_FROM_ALL)

# SD card (Phase 2): PICO_TOOLSET_BUILD_SDCARD gates cmake/pico_fatfs.cmake
# below (it needs the option ON before it runs). PICO_TOOLSET_SDCARD_STDIO
# stays OFF here (see config/CMakeLists.txt for why) -- config::PicoBootConfig
# compiles the stdio shim source directly into its own target instead.
set(PICO_TOOLSET_BUILD_SDCARD ON)
include(${TOOLSET_ROOT}/cmake/pico_fatfs.cmake)
add_subdirectory(${TOOLSET_ROOT}/components/sdcard ${CMAKE_BINARY_DIR}/_toolset/sdcard EXCLUDE_FROM_ALL)

# FatFs configuration override, generated from the fetched ffconf.h (its options
# are plain #defines, so they cannot be overridden from the command line):
#  - names as UTF-8 (FF_LFN_UNICODE=2): accented file names display and open
#    correctly instead of coming out as Japanese OEM bytes;
#  - OEM code page 850 (Western Europe) instead of 932 (Japanese), which also
#    drops ~55 KB of conversion tables from every bootloader.
# The generated directory goes first on the include path so ff.h finds it.
get_target_property(_fatfs_includes pico_fatfs INTERFACE_INCLUDE_DIRECTORIES)
foreach(_dir IN LISTS _fatfs_includes)
    if(EXISTS "${_dir}/ffconf.h")
        set(_fatfs_ffconf "${_dir}/ffconf.h")
    endif()
endforeach()
if(NOT _fatfs_ffconf)
    message(FATAL_ERROR "ffconf.h not found in pico_fatfs's include directories")
endif()
file(READ "${_fatfs_ffconf}" _ffconf_text)
string(REGEX REPLACE "#define FF_CODE_PAGE[ \t]+[0-9]+" "#define FF_CODE_PAGE 850" _ffconf_text "${_ffconf_text}")
string(REGEX REPLACE "#define FF_LFN_UNICODE[ \t]+[0-9]+" "#define FF_LFN_UNICODE 2" _ffconf_text "${_ffconf_text}")
file(WRITE "${CMAKE_BINARY_DIR}/fatfs_conf/ffconf.h" "${_ffconf_text}")
target_include_directories(pico_fatfs BEFORE INTERFACE "${CMAKE_BINARY_DIR}/fatfs_conf")

# USB HID host (PIO-USB keyboard/mouse): only where the board has the port
# and a free PIO (Waveshare RP2350-PiZero). Must be added *before*
# usb_composite so it can define its "_hid" variant.
if(PICOBOOT_BOARD STREQUAL "waveshare_pizero")
    set(PICO_TOOLSET_BUILD_USB_HID ON)
    set(PICO_TOOLSET_USB_HID_KEYMAPS us CACHE STRING "" FORCE) # keep flash small
    set(PICO_TOOLSET_USB_HID_DEFAULT_KEYMAP us CACHE STRING "" FORCE)
    include(${TOOLSET_ROOT}/cmake/pico_pio_usb.cmake)
    add_subdirectory(${TOOLSET_ROOT}/components/usb_hid ${CMAKE_BINARY_DIR}/_toolset/usb_hid EXCLUDE_FROM_ALL)
endif()

add_subdirectory(${TOOLSET_ROOT}/components/usb_composite ${CMAKE_BINARY_DIR}/_toolset/usb_composite EXCLUDE_FROM_ALL)

# LVGL UI: panel/touch/video drivers per board + the LVGL bridge.
add_subdirectory(${TOOLSET_ROOT}/components/driver_interfaces ${CMAKE_BINARY_DIR}/_toolset/driver_interfaces EXCLUDE_FROM_ALL)
if(PICOBOOT_BOARD STREQUAL "waveshare_pizero")
    add_subdirectory(${TOOLSET_ROOT}/components/ili9486 ${CMAKE_BINARY_DIR}/_toolset/ili9486 EXCLUDE_FROM_ALL)
endif()
if(PICOBOOT_BOARD STREQUAL "crowpanel_pico_hmi_28")
    add_subdirectory(${TOOLSET_ROOT}/components/st7789 ${CMAKE_BINARY_DIR}/_toolset/st7789 EXCLUDE_FROM_ALL)
endif()
if(NOT PICOBOOT_BOARD STREQUAL "pico_dv")
    add_subdirectory(${TOOLSET_ROOT}/components/xpt2046 ${CMAKE_BINARY_DIR}/_toolset/xpt2046 EXCLUDE_FROM_ALL)
endif()
if(NOT PICOBOOT_BOARD STREQUAL "crowpanel_pico_hmi_28")
    add_subdirectory(${TOOLSET_ROOT}/components/dvi_hdmi ${CMAKE_BINARY_DIR}/_toolset/dvi_hdmi EXCLUDE_FROM_ALL)
endif()
set(LV_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/ui/lvgl/lv_conf.h)
include(${TOOLSET_ROOT}/cmake/pico_lvgl.cmake)
if(NOT PICOBOOT_BOARD STREQUAL "waveshare_pizero")
    # 264 KB of RAM: a smaller LVGL heap (see ui/lvgl/lv_conf.h).
    target_compile_definitions(lvgl PUBLIC PICOBOOT_LV_MEM_KB=56)
endif()
add_subdirectory(${TOOLSET_ROOT}/components/lvgl_display ${CMAKE_BINARY_DIR}/_toolset/lvgl_display EXCLUDE_FROM_ALL)

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
elseif(PICOBOOT_BOARD STREQUAL "pico_dv")
    add_subdirectory(targets/picoboot_lvgl_dvi)
endif()
