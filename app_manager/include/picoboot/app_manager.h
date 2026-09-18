#pragma once

#include "picoboot/app_catalog.h"
#include "picoboot/config.h"
#include "picoboot/flash_writer.h"

#include "pico_toolset/sdcard.h"

namespace picoboot {

enum class LoadResult {
    kBooting,   // flashed (or skipped as identical) and about to reboot into the app -- never returns
    kTooLarge,
    kReadFailed,
};

// Orchestrates storage + config + boot_core for the "user picked an entry"
// flow. The only layer that talks to all three -- UI never touches
// flash/VTOR/watchdog directly, only through this.
class AppManager {
public:
    AppManager(pico_toolset::SdCard& sd_card, PicoBootConfig& config)
        : m_sd_card(sd_card), m_config(config) {}

    // Reads `entry` from the card, checks capacity, 4KB-memcmp-compares
    // against the currently-flashed image (skipping the erase/program if
    // identical, per spec), updates picoboot.cfg's last-run entry, and
    // reboots into the app via FastBoot::reboot_into_app() -- which never
    // returns. Only returns on a failure that aborts before touching flash
    // (too large, or the file couldn't be read).
    LoadResult load_and_boot(const AppBinaryEntry& entry, const ProgressSink& sink);

private:
    pico_toolset::SdCard& m_sd_card;
    PicoBootConfig& m_config;
};

} // namespace picoboot
