# Bootloader with the LVGL UI on the external 3.5" 480x320 LCD + XPT2046 touch of the
# Waveshare RP2350-PiZero (see Pico-Toolset boards/waveshare_rp2350_pizero_lcd35a.md).
# The same header wiring carries either controller, so the same main.cpp is built twice:
#   picoboot_lvgl_lcd          ILI9486 (Waveshare 3.5" RPi LCD (A))
#   picoboot_lvgl_lcd_st7796   ST7796U (e.g. SunFounder 3.5" IPS)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_sdcard_stdio.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_app_linker.cmake)

function(picoboot_add_lcd_target NAME PANEL_LIB)
    add_executable(${NAME} ${CMAKE_CURRENT_LIST_DIR}/main.cpp)
    target_include_directories(${NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)

    target_link_libraries(${NAME} PRIVATE
        pico_stdlib
        picoboot_boot_core
        picoboot_storage
        picoboot_config
        picoboot_app_manager
        picoboot_usb
        pico_toolset_usb_composite_hid # device stack + PIO-USB host share one tusb_config
        pico_toolset_lvgl_hid
        picoboot_ui_lvgl
        picoboot_ui_serial
        ${PANEL_LIB}
        pico_toolset_xpt2046
    )

    picoboot_link_sdcard_stdio_shim(${NAME})

    pico_enable_stdio_usb(${NAME} OFF)
    pico_enable_stdio_uart(${NAME} OFF)
    pico_add_extra_outputs(${NAME})

    picoboot_set_bootloader_flash_region(${NAME})

    # Core 0's stack: the default 2 KiB is tight for FatFs + printf + LVGL (the 'info'
    # serial command reports the headroom).
    target_compile_definitions(${NAME} PRIVATE PICO_STACK_SIZE=0x1000)
endfunction()

picoboot_add_lcd_target(picoboot_lvgl_lcd pico_toolset_ili9486)

picoboot_add_lcd_target(picoboot_lvgl_lcd_st7796 pico_toolset_st7796)
target_compile_definitions(picoboot_lvgl_lcd_st7796 PRIVATE PICOBOOT_LCD_ST7796=1)
