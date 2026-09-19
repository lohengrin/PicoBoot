#pragma once

#include "picoboot/boot_tags.h"

namespace picoboot {

// Thin wrapper over pico_toolset::watchdog_reboot_with_tag()/
// consume_pending_watchdog_tag() that speaks in picoboot::BootTag instead
// of a raw uint32_t, so callers (main(), app_manager, the test apps) never
// touch the watchdog scratch registers directly.
class FastBoot {
public:
    // Checked once, first thing in main(), before any other init. Returns
    // false on a cold power-on. On true, `out_tag` holds the reason.
    static bool consume(BootTag& out_tag);

    // Reboots straight into the app at kAppFlashBase on the next power-on
    // (FastBoot::consume() will return kBootApp). Never returns.
    [[noreturn]] static void reboot_into_app();

    // Same, for a normal build that must be launched through flash address
    // translation (BootTag::kBootAppRemapped, RP2350 only).
    [[noreturn]] static void reboot_into_app_remapped();

    // Call first thing in main() with the tag from consume(): if it asks for
    // the application to be started, does so and never returns; otherwise
    // returns and the full bootloader proceeds.
    static void boot_app_if_tagged(BootTag tag);

    // Reboots back to the full bootloader UI (equivalent to a cold boot,
    // distinguished only for a possible future "returned from app" banner).
    // Never returns.
    [[noreturn]] static void reboot_into_bootloader();

    // Reboots into the ROM's USB BOOTSEL mass-storage mode (no button
    // needed), e.g. to update the bootloader itself. Never returns.
    [[noreturn]] static void reboot_into_bootsel();
};

} // namespace picoboot
