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

#if PICO_RP2350
// RP2350 only. Launches an application that was linked normally (at
// 0x10000000) but is flashed at `app_flash_base` (the partition): programs
// the flash controller's address translation so the partition appears at
// 0x10000000 for the whole partition (`partition_bytes`), flushes the XIP
// cache, then relocates VTOR to 0x10000000 and jumps. The bootloader's own
// code disappears from the XIP window at that moment, so the switch runs from
// RAM. Never returns.
//
// The translation is undone by the next reset. Caution for such apps: flash
// *programming* APIs take physical flash offsets, which are not translated --
// an app that writes flash at low offsets is writing over the bootloader.
[[noreturn]] void jump_to_app_remapped(uint32_t app_flash_base, uint32_t partition_bytes);
#endif

} // namespace picoboot
