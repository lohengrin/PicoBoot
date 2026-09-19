#pragma once

#include "picoboot/app_catalog.h"
#include "picoboot/config.h"
#include "picoboot/flash_writer.h"

#include "pico_toolset/sdcard.h"

namespace picoboot {

enum class LoadResult {
    kBooting,   // never actually returned: success reboots into the app
    kTooLarge,
    kReadFailed,
    kFlashFailed, // flash was (partly) written and is not bootable
};

// Orchestrates storage + config + boot_core. The only layer that talks to
// all three -- UI never touches flash/VTOR/watchdog directly, only through
// this. Owns the catalog and config so every UI backend shares one view.
class AppManager {
public:
    AppManager(pico_toolset::SdCard& sd_card, const pico_toolset::SdCardConfig& sd_config)
        : m_sd_card(sd_card), m_sd_config(sd_config) {}

    // (Re)mounts the card, rescans the catalog and reloads picoboot.cfg.
    // Always remounts: a host may have changed the volume over USB MSC and
    // FatFs caches FAT state. Returns whether a card is mounted.
    bool refresh();

    [[nodiscard]] bool card_present() const { return m_sd_card.is_mounted(); }
    [[nodiscard]] const AppCatalog& catalog() const { return m_catalog; }
    [[nodiscard]] const PicoBootConfig& config() const { return m_config; }

    // Reads `entry`, checks capacity, 4KB-compares against the flashed
    // image (skipping erase/program if identical), records it as last-run in
    // picoboot.cfg and reboots into it. Only returns on failure before
    // flash is touched.
    LoadResult load_and_boot(const AppBinaryEntry& entry, const ProgressSink& sink);

private:
    pico_toolset::SdCard& m_sd_card;
    pico_toolset::SdCardConfig m_sd_config;
    AppCatalog m_catalog;
    PicoBootConfig m_config;
};

} // namespace picoboot
