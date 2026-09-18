#pragma once

#include <cstdint>

// Shared by the bootloader (boot_core::FastBoot) and any target app that
// wants to request a return to the bootloader (see testapps/include/
// picoboot_app_api.h) -- both sides must agree on these values, so they
// live in exactly one header rather than being redefined per consumer.
// Riding on pico_toolset::watchdog_reboot_with_tag()/
// consume_pending_watchdog_tag(), which already owns watchdog_hw->scratch[0,1]
// (see Pico-Toolset's scratch-register allocation table) -- PicoBoot does
// not claim a new scratch index for this.
namespace picoboot {

enum class BootTag : uint32_t {
    // The only tag that skips full bootloader init: jump straight into the
    // app at kAppFlashBase. Kept as a single equality check in FastBoot
    // since it runs before anything exists to report a fault if it's wrong.
    kBootApp = 1,

    // App is asking to come back to the bootloader UI. Falls through to the
    // exact same full-init path as a cold power-on.
    kReenterBootloader = 2,
};

} // namespace picoboot
