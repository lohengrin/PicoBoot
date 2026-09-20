# usb: composite CDC+MSC bridge. Independent infrastructure -- nothing
# else in PicoBoot depends on it besides the target's main loop.
add_library(picoboot_usb STATIC
    src/usb_bridge.cpp
)
target_include_directories(picoboot_usb PUBLIC include)
# Only the composite's *header* is needed here. The executable links exactly
# one composite variant (pico_toolset_usb_composite, or _hid when the HID
# host also runs) so the TinyUSB config matches its own TinyUSB core.
target_include_directories(picoboot_usb PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../third_party/pico-toolset/components/usb_composite/include
)
target_link_libraries(picoboot_usb PUBLIC
    pico_stdlib
    pico_toolset_sdcard
    picoboot_storage
)
