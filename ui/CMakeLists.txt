# ui: BootUi interface + backends. Depends on app_manager only.
add_library(picoboot_ui_serial STATIC
    serial/serial_ui.cpp
)
target_include_directories(picoboot_ui_serial PUBLIC include serial)
target_link_libraries(picoboot_ui_serial PUBLIC
    pico_stdlib
    picoboot_app_manager
)

# LVGL UI: only configured when the target build provides the lvgl target
# and pico_toolset_lvgl_display (see targets/picoboot_lvgl_lcd).
if(TARGET pico_toolset_lvgl_display)
    add_library(picoboot_ui_lvgl STATIC lvgl/lvgl_ui.cpp lvgl/screenshot.cpp)
    target_include_directories(picoboot_ui_lvgl PUBLIC include lvgl)
    target_link_libraries(picoboot_ui_lvgl PUBLIC
        pico_stdlib
        picoboot_app_manager
        pico_toolset_lvgl_display
    )
endif()
