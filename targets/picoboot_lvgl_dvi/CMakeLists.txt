# Bootloader with the LVGL UI on the Waveshare RP2350-PiZero's onboard
# HDMI/DVI output (PIO-driven TMDS, encoded on core 1). No touch panel on
# this variant; the serial UI runs alongside for control.
add_executable(picoboot_lvgl_dvi main.cpp)

target_link_libraries(picoboot_lvgl_dvi PRIVATE
    pico_stdlib
    pico_multicore
    picoboot_boot_core
    picoboot_storage
    picoboot_config
    picoboot_app_manager
    picoboot_usb
    pico_toolset_usb_composite_hid  # device stack + PIO-USB host share one tusb_config
    pico_toolset_lvgl_hid
    picoboot_ui_lvgl
    picoboot_ui_serial
    pico_toolset_dvi_hdmi
)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_sdcard_stdio.cmake)
picoboot_link_sdcard_stdio_shim(picoboot_lvgl_dvi)

pico_enable_stdio_usb(picoboot_lvgl_dvi OFF)
pico_enable_stdio_uart(picoboot_lvgl_dvi OFF)
pico_add_extra_outputs(picoboot_lvgl_dvi)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_app_linker.cmake)
picoboot_set_bootloader_flash_region(picoboot_lvgl_dvi)
