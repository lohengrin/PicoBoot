# picoboot_set_app_flash_region(TARGET TOTAL_FLASH_SIZE)
#
# Overrides TARGET's FLASH linker region to start at
# cmake/picoboot_flash_layout.cmake's PICOBOOT_APP_FLASH_ORIGIN instead of
# pico-sdk's default 0x10000000, with LENGTH = TOTAL_FLASH_SIZE minus the
# bootloader's reserve -- following the same technique pico-sdk's own
# pico_override_flash_size() uses (a generated pico_flash_region.ld
# override path), just also overriding ORIGIN, not only LENGTH.
#
# TOTAL_FLASH_SIZE is the board's real total flash size in bytes (e.g.
# PICO_FLASH_SIZE_BYTES from that board's header) -- passed explicitly
# rather than auto-detected, matching pico_override_flash_size()'s own
# convention, since it varies per board and per-app CMakeLists.txt already
# needs to pick a PICO_BOARD.
#
# Note: this file's own directory is captured at include() time, below --
# CMAKE_CURRENT_LIST_DIR inside a function body reflects the *caller's*
# listfile, not this file's, so it cannot be used directly inside the
# function itself.
set(PICOBOOT_APP_LINKER_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR})

function(picoboot_set_app_flash_region TARGET TOTAL_FLASH_SIZE)
    include(${PICOBOOT_APP_LINKER_CMAKE_DIR}/picoboot_flash_layout.cmake)

    math(EXPR PICOBOOT_APP_FLASH_LENGTH "${TOTAL_FLASH_SIZE} - ${PICOBOOT_BOOT_RESERVE_SIZE}")

    configure_file(
        ${PICOBOOT_APP_LINKER_CMAKE_DIR}/../testapps/linker/picoboot_app_flash_region.template.ld
        ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}/pico_flash_region.ld
        @ONLY
    )
    pico_add_linker_script_override_path(${TARGET} ${CMAKE_CURRENT_BINARY_DIR}/${TARGET} FILES pico_flash_region.ld)
    target_compile_definitions(${TARGET} PRIVATE
        "PICOBOOT_APP_FLASH_ORIGIN=${PICOBOOT_APP_FLASH_ORIGIN}"
        "PICO_FLASH_SIZE_BYTES=${TOTAL_FLASH_SIZE}"
    )
endfunction()
