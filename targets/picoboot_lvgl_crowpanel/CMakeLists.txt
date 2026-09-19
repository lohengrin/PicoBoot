# Bootloader with the LVGL UI on the CrowPanel PICO HMI 2.8" (RP2040):
# 320x240 ST7789 + XPT2046 touch + uSD, all on SPI1. No USB host on this
# board; the serial UI runs alongside for control.
add_executable(picoboot_lvgl_crowpanel main.cpp)
target_include_directories(picoboot_lvgl_crowpanel PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)

target_link_libraries(picoboot_lvgl_crowpanel PRIVATE
    pico_stdlib
    picoboot_boot_core
    picoboot_storage
    picoboot_config
    picoboot_app_manager
    picoboot_usb
    pico_toolset_usb_composite
    picoboot_ui_lvgl
    picoboot_ui_serial
    pico_toolset_st7789
    pico_toolset_xpt2046
)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_sdcard_stdio.cmake)
picoboot_link_sdcard_stdio_shim(picoboot_lvgl_crowpanel)

pico_enable_stdio_usb(picoboot_lvgl_crowpanel OFF)
pico_enable_stdio_uart(picoboot_lvgl_crowpanel OFF)
pico_add_extra_outputs(picoboot_lvgl_crowpanel)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_app_linker.cmake)
picoboot_set_bootloader_flash_region(picoboot_lvgl_crowpanel)

# Print raw and mapped coordinates over serial on every touch, to derive this
# board's calibration (see main.cpp). Set to OFF once calibrated.
option(PICOBOOT_TOUCH_DEBUG "Print touch samples over serial" ON)
if(PICOBOOT_TOUCH_DEBUG)
    target_compile_definitions(pico_toolset_lvgl_display PRIVATE PICO_TOOLSET_LVGL_TOUCH_DEBUG=1)
endif()
