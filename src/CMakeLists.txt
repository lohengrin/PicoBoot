# PicoBoot libraries, one per layer. All public headers live in the root include/
# (include/picoboot/*.h); each layer's sources are in src/<layer>/. Dependencies flow
# downward only: targets -> ui -> app_manager -> {storage, config, boot_core}; usb is
# independent infrastructure polled by the target's main loop.

# --- boot_core: safety-critical HAL (fast-boot path, flash writer, VTOR jump). PicoBoot-only;
# never depends on storage/usb/ui. Depends only on the watchdog-tag reboot primitive and pico-sdk.
include(${PROJECT_SOURCE_DIR}/cmake/picoboot_flash_layout.cmake)
configure_file(
    ${PROJECT_SOURCE_DIR}/include/picoboot/flash_layout.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/picoboot/flash_layout.h
    @ONLY
)
add_library(picoboot_boot_core STATIC
    boot_core/fastboot.cpp
    boot_core/vtor_jump.cpp
    boot_core/flash_writer.cpp
    boot_core/image_check.cpp
    boot_core/memory_probe.cpp
)
target_include_directories(picoboot_boot_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
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

# --- storage: AppCatalog over the SD card component -- no boot_core/usb/ui dependency.
add_library(picoboot_storage STATIC
    storage/app_catalog.cpp
    storage/storage_state.cpp
)
target_include_directories(picoboot_storage PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(picoboot_storage PUBLIC
    pico_stdlib
    pico_toolset_sdcard
)

# --- config: picoboot.cfg read/write via FatFs's newlib syscall shim (fopen/fgets/fprintf);
# pico_toolset::SdCard itself is read-only.
#
# picoboot_config calls fopen()/fgets()/fprintf(), which need sdcard_stdio.cpp's
# _stat/_open/_read/_write/... definitions -- but see the *executable* target's CMakeLists.txt
# (cmake/picoboot_sdcard_stdio.cmake) for why that file must be a plain source of the final
# executable, not bundled into a static library: newlib only demands those symbols once its
# own implicit archive is scanned, at the very end of the link line, after which GNU ld never
# revisits an earlier archive. Only a plain object file sidesteps that ordering problem.
add_library(picoboot_config STATIC config/config.cpp)
target_include_directories(picoboot_config PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(picoboot_config PUBLIC
    pico_stdlib
    pico_toolset_sdcard
)

# --- app_manager: orchestrates storage + config + boot_core for "user picked an entry" --
# the only layer that talks to all three.
add_library(picoboot_app_manager STATIC app_manager/app_manager.cpp)
target_include_directories(picoboot_app_manager PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(picoboot_app_manager PUBLIC
    pico_stdlib
    picoboot_boot_core
    picoboot_storage
    picoboot_config
)

# --- usb: composite CDC+MSC bridge. Nothing else depends on it besides the target's main loop.
add_library(picoboot_usb STATIC usb/usb_bridge.cpp)
target_include_directories(picoboot_usb PUBLIC ${PROJECT_SOURCE_DIR}/include)
# Only the composite's *header* is needed here. The executable links exactly one composite
# variant (pico_toolset_usb_composite, or _hid when the HID host also runs) so the TinyUSB
# config matches its own TinyUSB core.
target_include_directories(picoboot_usb PUBLIC ${TOOLSET_ROOT}/components/usb_composite/include)
target_link_libraries(picoboot_usb PUBLIC
    pico_stdlib
    pico_toolset_sdcard
    picoboot_storage
)

# --- ui: BootUi interface + backends. Depend on app_manager only.
add_library(picoboot_ui_serial STATIC ui/serial/serial_ui.cpp)
target_include_directories(picoboot_ui_serial PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(picoboot_ui_serial PUBLIC
    pico_stdlib
    picoboot_app_manager
)

# LVGL UI: only configured when the board build provides pico_toolset_lvgl_display.
# Its lv_conf.h is src/ui/lvgl/lv_conf.h (LV_CONF_PATH in the top-level CMakeLists.txt).
if(TARGET pico_toolset_lvgl_display)
    add_library(picoboot_ui_lvgl STATIC
        ui/lvgl/lvgl_ui.cpp
        ui/lvgl/screenshot.cpp
    )
    target_include_directories(picoboot_ui_lvgl PUBLIC ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(picoboot_ui_lvgl PUBLIC
        pico_stdlib
        picoboot_app_manager
        pico_toolset_lvgl_display
    )
endif()
