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

# Core 0's stack: the default 2 KiB is tight for FatFs + printf + LVGL (the 'info'
# serial command reports the headroom).
target_compile_definitions(picoboot_lvgl_crowpanel PRIVATE PICO_STACK_SIZE=0x1000)
