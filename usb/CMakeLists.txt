# usb: composite CDC+MSC bridge. Independent infrastructure -- nothing
# else in PicoBoot depends on it besides the target's main loop.
add_library(picoboot_usb STATIC
    src/usb_bridge.cpp
)
target_include_directories(picoboot_usb PUBLIC include)
target_link_libraries(picoboot_usb PUBLIC
    pico_stdlib
    pico_toolset_usb_composite
    pico_toolset_sdcard
)
