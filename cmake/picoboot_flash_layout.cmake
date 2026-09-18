# Single source of truth for the flash boundary between PicoBoot and the
# application partition. Included by both PicoBoot's own top-level
# CMakeLists.txt (which configures boot_core/include/picoboot/
# flash_layout.h.in from these values) and each standalone testapp's
# CMakeLists.txt (which uses them directly to override its linker script's
# FLASH region) -- so the two independently-configured CMake projects
# (PicoBoot itself, and every app it will ever boot) can never drift apart.
# Never hand-copy these numbers anywhere else.

set(PICOBOOT_FLASH_XIP_BASE 0x10000000)

# Reserved for PicoBoot itself, sized for the *largest* UI variant
# (picoboot_lvgl_dvi), not the smallest -- see docs/architecture.md and
# boot_core/include/picoboot/flash_layout.h.in for full rationale.
# Revisit only with real picoboot_lvgl_dvi .uf2 sizes in hand (Phase 7).
set(PICOBOOT_BOOT_RESERVE_SIZE 0x80000) # 512 KiB

math(EXPR PICOBOOT_APP_FLASH_ORIGIN_DEC "${PICOBOOT_FLASH_XIP_BASE} + ${PICOBOOT_BOOT_RESERVE_SIZE}")
math(EXPR PICOBOOT_APP_FLASH_ORIGIN "${PICOBOOT_APP_FLASH_ORIGIN_DEC}" OUTPUT_FORMAT HEXADECIMAL)

set(PICOBOOT_FLASH_SECTOR_SIZE 4096)
