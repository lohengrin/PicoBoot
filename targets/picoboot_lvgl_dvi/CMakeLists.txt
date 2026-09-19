# Bootloader with the LVGL UI on HDMI/DVI (PIO-driven TMDS, encoded on core 1):
# Waveshare RP2350-PiZero (USB keyboard/mouse) or Pico DV + Pico (W) (3 buttons).
# The serial UI runs alongside for control.
add_executable(picoboot_lvgl_dvi main.cpp)
target_include_directories(picoboot_lvgl_dvi PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)

target_link_libraries(picoboot_lvgl_dvi PRIVATE
    pico_stdlib
    pico_multicore
    picoboot_boot_core
    picoboot_storage
    picoboot_config
    picoboot_app_manager
    picoboot_usb
    picoboot_ui_lvgl
    picoboot_ui_serial
    pico_toolset_dvi_hdmi
)

if(PICOBOOT_BOARD STREQUAL "waveshare_pizero")
    # PIO-USB host (keyboard/mouse): device stack + host share one tusb_config.
    target_link_libraries(picoboot_lvgl_dvi PRIVATE pico_toolset_usb_composite_hid pico_toolset_lvgl_hid)
else()
    target_link_libraries(picoboot_lvgl_dvi PRIVATE pico_toolset_usb_composite)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_sdcard_stdio.cmake)
picoboot_link_sdcard_stdio_shim(picoboot_lvgl_dvi)

pico_enable_stdio_usb(picoboot_lvgl_dvi OFF)
pico_enable_stdio_uart(picoboot_lvgl_dvi OFF)
pico_add_extra_outputs(picoboot_lvgl_dvi)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_app_linker.cmake)
picoboot_set_bootloader_flash_region(picoboot_lvgl_dvi)
