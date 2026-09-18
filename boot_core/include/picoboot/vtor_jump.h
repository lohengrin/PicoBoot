#pragma once

#include <cstdint>

namespace picoboot {

// Relocates the vector table to an app flashed at `app_flash_base` and
// jumps into it. Only ever called from the fast-boot path (see FastBoot)
// on a power-on that carries BootTag::kBootApp -- never in-process from the
// full bootloader, since the spec wants the watchdog soft-reboot's pristine
// hardware state (default clocks/cores/GPIO/RAM) before the app runs.
//
// The app's real vector table (word 0 = initial SP, word 1 = reset
// handler) must sit at `app_flash_base` plus a chip-dependent offset --
// see this .cpp's kVectorTableOffset comment -- the caller (FlashWriter)
// is responsible for having written a real app image there; this function
// does not validate it.
[[noreturn]] void relocate_vtor_and_jump(uint32_t app_flash_base);

} // namespace picoboot
