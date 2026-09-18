# picoboot_link_sdcard_stdio_shim(TARGET)
#
# Adds Pico-Toolset's FatFs newlib-syscall shim (sdcard_stdio.cpp -- fopen/
# fread/fwrite/stat/... over FatFs) as a PLAIN SOURCE of the given
# executable target, not linked via any intermediate STATIC library
# (picoboot_config, pico_toolset_sdcard, or otherwise).
#
# This must be a plain object of the final executable, not an archive
# member: newlib's own _stat/_open/_read/_write/... implementations are
# only demanded once libc's own archive is scanned, which the compiler
# driver places implicitly at the very end of the link line -- by then any
# earlier STATIC library archive containing this shim's definitions has
# already been passed, and GNU ld's single left-to-right archive scan
# never revisits an earlier archive for a symbol need that only surfaces
# later. A plain object file has no such "pulled on demand" behavior: it's
# always included, so the ordering problem doesn't arise. (An attempted
# alternative, --whole-archive over pico_toolset_sdcard, was rejected: it
# force-pulls pico-sdk's hardware_pio, an INTERFACE library that adds
# pio.c as a *source* rather than a prebuilt object -- so it gets compiled
# once per consumer already, and duplicates when also unconditionally
# pulled from inside the forced archive.)
#
# Call this once, from the CMakeLists.txt of any executable that (directly
# or transitively via picoboot_config/picoboot_storage) uses stdio-style
# SD card file access.
set(PICOBOOT_SDCARD_STDIO_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR})

function(picoboot_link_sdcard_stdio_shim TARGET)
    target_sources(${TARGET} PRIVATE
        ${PICOBOOT_SDCARD_STDIO_CMAKE_DIR}/../third_party/pico-toolset/components/sdcard/src/sdcard_stdio.cpp
    )
endfunction()
