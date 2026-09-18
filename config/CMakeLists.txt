# config: picoboot.cfg read/write via FatFs's newlib syscall shim
# (fopen/fgets/fprintf) -- pico_toolset::SdCard itself is read-only.
#
# picoboot_config itself calls fopen()/fgets()/fprintf(), which need
# sdcard_stdio.cpp's _stat/_open/_read/_write/... definitions to be linked
# in -- but see the *executable* target's CMakeLists.txt (e.g.
# targets/picoboot_serial) for why that file must be added as a plain
# source of the final executable, not bundled into this (or any) static
# library: newlib only demands those symbols once its own implicit archive
# is scanned, which happens at the very end of the link line, by which
# point any earlier STATIC library archive containing them has already
# been passed and GNU ld's single left-to-right scan never revisits it.
# Only a plain object file (unconditionally included, not "pulled on
# demand" the way archive members are) sidesteps that ordering problem.
add_library(picoboot_config STATIC
    src/config.cpp
)

target_include_directories(picoboot_config PUBLIC include)

target_link_libraries(picoboot_config PUBLIC
    pico_stdlib
    pico_toolset_sdcard
)
