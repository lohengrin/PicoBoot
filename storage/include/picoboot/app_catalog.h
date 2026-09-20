#pragma once

#include "pico_toolset/sdcard.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace picoboot {

struct AppBinaryEntry {
    std::string filename;
    uint32_t size_bytes = 0;
};

// Pageable catalog of compatible (.bin) files on the SD card. Both UI
// backends (SerialUi's numbered/paged menu, LvglUi's listview) page
// through this single source of truth rather than each re-listing the
// card, so a "refresh" always means the same thing everywhere.
class AppCatalog {
public:
    // Re-scans the card's root directory for .bin files, replacing any
    // previous listing. Call on startup and whenever the UI's "refresh"
    // action fires (including after a host has drag-and-dropped a new
    // file over USB MSC -- see docs/architecture.md's "manual refresh,
    // not live coherency" model).
    void refresh(const pico_toolset::SdCard& sd_card);

    // Directory listings are bounded so a huge folder cannot exhaust the heap of
    // a small chip (the Pico DV has ~59 KB free).
    static constexpr size_t kMaxEntries = 512;

    [[nodiscard]] size_t count() const { return m_entries.size(); }
    // True if the card holds more .bin files than kMaxEntries (the rest are not listed).
    [[nodiscard]] bool truncated() const { return m_truncated; }

    // Zero-based, clamped to available entries -- callers don't need to
    // range-check start/count themselves.
    [[nodiscard]] std::span<const AppBinaryEntry> page(size_t start, size_t count) const;

    [[nodiscard]] const AppBinaryEntry* find(size_t index) const;

private:
    std::vector<AppBinaryEntry> m_entries;
    bool m_truncated = false;
};

} // namespace picoboot
