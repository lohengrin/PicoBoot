# ui: BootUi interface + backends. Depends on app_manager only.
add_library(picoboot_ui_serial STATIC
    serial/serial_ui.cpp
)
target_include_directories(picoboot_ui_serial PUBLIC include serial)
target_link_libraries(picoboot_ui_serial PUBLIC
    pico_stdlib
    picoboot_app_manager
)
