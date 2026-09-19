# Bootloader with the serial (CDC) UI.
add_executable(picoboot_serial
    main.cpp
)

target_link_libraries(picoboot_serial PRIVATE
    pico_stdlib
    picoboot_boot_core
    picoboot_storage
    picoboot_config
    picoboot_app_manager
    picoboot_usb
    picoboot_ui_serial
)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_sdcard_stdio.cmake)
picoboot_link_sdcard_stdio_shim(picoboot_serial)

# CDC comes from pico_toolset_usb_composite (via picoboot_usb), not pico_stdio_usb.
pico_enable_stdio_usb(picoboot_serial OFF)
pico_enable_stdio_uart(picoboot_serial OFF)
pico_add_extra_outputs(picoboot_serial)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_app_linker.cmake)
picoboot_set_bootloader_flash_region(picoboot_serial)
