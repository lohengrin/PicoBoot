# Phase 1: hardcoded fake menu over stock pico_stdio_usb (single CDC). No
# SD card, no USB composite, no real flash writing yet -- proves the build
# skeleton and the boot_core link before layering in real logic (Phase 2+).
add_executable(picoboot_serial
    main.cpp
)

target_link_libraries(picoboot_serial PRIVATE
    pico_stdlib
    picoboot_boot_core
)

pico_enable_stdio_usb(picoboot_serial ON)
pico_enable_stdio_uart(picoboot_serial OFF)
pico_add_extra_outputs(picoboot_serial)
