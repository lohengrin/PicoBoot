# Bootloader with the LVGL UI on the Pimoroni Presto's built-in 480x480 RGB-parallel LCD
# (ST7701, scanned out from an 8-bit palette framebuffer by core 1) + FT6236 touch + uSD.
# The serial (CDC) UI runs alongside for control.
add_executable(picoboot_lvgl_presto main.cpp)

target_link_libraries(picoboot_lvgl_presto PRIVATE
    pico_stdlib
    pico_multicore
    picoboot_boot_core
    picoboot_storage
    picoboot_config
    picoboot_app_manager
    picoboot_usb
    pico_toolset_usb_composite
    picoboot_ui_lvgl
    picoboot_ui_serial
    pico_toolset_st7701
    pico_toolset_ft6236
    picoboot_bluetooth # BLE keyboard / mouse / gamepad, QwSTPad, pairing panel
)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_sdcard_stdio.cmake)
picoboot_link_sdcard_stdio_shim(picoboot_lvgl_presto)

pico_enable_stdio_usb(picoboot_lvgl_presto OFF)
pico_enable_stdio_uart(picoboot_lvgl_presto OFF)
pico_add_extra_outputs(picoboot_lvgl_presto)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_app_linker.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_flash_layout.cmake)

# The Bluetooth bond store stays at the SDK default place (the last sectors of flash), shared with
# every other toolset application; the app partition stops before it (PICOBOOT_FLASH_TAIL_RESERVE_SIZE).
# Do not set PICO_FLASH_BANK_*: bluetooth_store.cpp rejects it at build time.
picoboot_set_bootloader_flash_region(picoboot_lvgl_presto)
target_compile_definitions(picoboot_lvgl_presto PRIVATE
    # Bonding writes flash from core 0 while core 1 only runs RAM-resident scanout code, so the
    # SDK need not (and cannot: core 1 never registers a lockout) stop it.
    PICO_FLASH_ASSUME_CORE1_SAFE=1
)

# Core 0's stack: the default 2 KiB is tight for FatFs + printf + LVGL (the 'info'
# serial command reports the headroom).
target_compile_definitions(picoboot_lvgl_presto PRIVATE PICO_STACK_SIZE=0x1000)
