# Bootloader with the LVGL UI on the external 3.5" ILI9486 LCD + XPT2046
# touch (Waveshare RP2350-PiZero + LCD35A, see Pico-Toolset boards/).
add_executable(picoboot_lvgl_lcd main.cpp)

target_link_libraries(picoboot_lvgl_lcd PRIVATE
    pico_stdlib
    picoboot_boot_core
    picoboot_storage
    picoboot_config
    picoboot_app_manager
    picoboot_usb
    pico_toolset_usb_composite
    picoboot_ui_lvgl
    picoboot_ui_serial
    pico_toolset_ili9486
    pico_toolset_xpt2046
)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_sdcard_stdio.cmake)
picoboot_link_sdcard_stdio_shim(picoboot_lvgl_lcd)

pico_enable_stdio_usb(picoboot_lvgl_lcd OFF)
pico_enable_stdio_uart(picoboot_lvgl_lcd OFF)
pico_add_extra_outputs(picoboot_lvgl_lcd)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_app_linker.cmake)
picoboot_set_bootloader_flash_region(picoboot_lvgl_lcd)
