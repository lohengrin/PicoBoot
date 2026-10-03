#pragma once

#include "picoboot/app_catalog.h"
#include "picoboot/config.h"
#include "picoboot/flash_writer.h"

#include "pico_toolset/sdcard.h"

#include <string>

namespace picoboot {

// Why the card is not usable (or that it is), from the last mount attempt.
enum class CardStatus {
    kReady,
    kNoCard,               // does not answer: missing, or wiring
    kUnsupportedFilesystem, // answers, but no FAT/exFAT volume FatFs can mount (GPT, unformatted, ...)
    kError,                // some other mount failure
};

enum class LoadResult {
    kBooting,   // never actually returned: success reboots into the app
    kTooLarge,
    kReadFailed,
    kInvalidImage, // not linked for the application partition (wrong address / not an image)
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

    // Folder navigation (one level per view): enter the folder at catalog index /
    // go to the parent, then re-list. Return false when not possible (not a folder,
    // already at the root). up() reports the folder left, to reselect it.
    bool browse_into(size_t index);
    bool browse_up(std::string* left = nullptr);
    void browse_root();

    // Next unused "screenshot_NNNN.bmp" in the card root (never overwrites an earlier one);
    // empty if the card is not usable.
    [[nodiscard]] std::string next_screenshot_path();

    // True only while the card is mounted and still answering.
    [[nodiscard]] bool card_present() const;
    [[nodiscard]] CardStatus card_status() const;
    // One line for the UIs: "no uSD card", "uSD card: no FAT/exFAT volume ...", ...
    [[nodiscard]] std::string card_message() const;
    // Multi-line diagnostics: card, filesystem result, capacity, listing, stack/heap headroom.
    [[nodiscard]] std::string describe_storage() const;
    [[nodiscard]] const AppCatalog& catalog() const { return m_catalog; }
    [[nodiscard]] const PicoBootConfig& config() const { return m_config; }

    // Inspects `entry` (chip family and link address, from the file itself),
    // streams it into flash (only differing sectors are written), records it
    // as last-run in picoboot.cfg and reboots into it -- through flash address
    // translation for a normal build on RP2350. Only returns on failure; then
    // last_error() says why.
    LoadResult load_and_boot(const AppBinaryEntry& entry, const ProgressSink& sink);

    // After load_and_boot() returned a failure: why, in words. The long form
    // is a full sentence (serial console); the short form fits one line of a
    // small screen.
    [[nodiscard]] const std::string& last_error() const { return m_error_long; }
    [[nodiscard]] const std::string& last_error_short() const { return m_error_short; }

private:
    pico_toolset::SdCard& m_sd_card;
    pico_toolset::SdCardConfig m_sd_config;
    AppCatalog m_catalog;
    unsigned m_screenshot_counter = 0; // last number handed out
    PicoBootConfig m_config;
    bool m_dir_from_config = false; // the first listing starts in last_run's folder
    std::string m_error_long;
    std::string m_error_short;

    LoadResult fail(LoadResult result, std::string short_text, std::string long_text);
};

} // namespace picoboot
