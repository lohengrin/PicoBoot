# app_manager: orchestrates storage + config + boot_core for "user picked
# an entry" -- the only layer that talks to all three.
add_library(picoboot_app_manager STATIC
    src/app_manager.cpp
)

target_include_directories(picoboot_app_manager PUBLIC include)

target_link_libraries(picoboot_app_manager PUBLIC
    pico_stdlib
    picoboot_boot_core
    picoboot_storage
    picoboot_config
)
