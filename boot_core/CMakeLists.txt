# boot_core: safety-critical HAL (fast-boot path, flash writer, VTOR jump).
# PicoBoot-only -- never depends on storage/usb/ui. Depends only on
# pico_toolset_reset_buttons (watchdog-tag reboot primitive) and pico-sdk.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/picoboot_flash_layout.cmake)

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/include/picoboot/flash_layout.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/picoboot/flash_layout.h
    @ONLY
)

add_library(picoboot_boot_core STATIC
    src/fastboot.cpp
    src/vtor_jump.cpp
    src/flash_writer.cpp
)

target_include_directories(picoboot_boot_core PUBLIC
    include
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(picoboot_boot_core PUBLIC
    pico_stdlib
    hardware_flash
    hardware_sync
    pico_flash
    pico_multicore
    pico_toolset_reset_buttons
)
