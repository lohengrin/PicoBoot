#pragma once

#include "pico_toolset/sdcard.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace picoboot {

struct AppBinaryEntry {
    std::string filename;   // bare name, as displayed
    std::string path;       // from the card root, '/'-separated, no leading slash ("games/a.bin")
    uint32_t size_bytes = 0;
    bool is_dir = false;
};

// Pageable catalog of one directory of the SD card: its sub-folders (first) and
// compatible (.bin) files. One level per view -- the UIs navigate with enter()/up()
// and re-list, so memory stays bounded whatever the card holds. Both UI
// backends (SerialUi's numbered/paged menu, LvglUi's listview) page
// through this single source of truth rather than each re-listing the
// card, so a "refresh" always means the same thing everywhere.
class AppCatalog {
public:
    // Re-scans the current directory (the root until navigated), replacing any
    // previous listing. If the directory is gone (host deleted it, card swapped)
    // it falls back to the root. Call on startup and whenever the UI's "refresh"
    // action fires (including after a host has drag-and-dropped a new
    // file over USB MSC -- see docs/architecture.md's "manual refresh,
    // not live coherency" model).
    // Blocking, but it calls the yield hook (set_yield) between slices of a few milliseconds, so a
    // USB host keeps being served while a big folder is read.
    void refresh(const pico_toolset::SdCard& sd_card);

    // The same scan in steps, for startup: begin_refresh() opens the folder, step() reads entries for
    // at most `budget_us` and returns true while more remain (the catalog is complete and sorted once
    // it returns false). The caller serves USB (and the UI) between steps. A new begin_refresh()
    // or refresh() abandons a scan in progress. While scanning() the entries are partial and unsorted.
    void begin_refresh(const pico_toolset::SdCard& sd_card);
    bool step(uint32_t budget_us);
    [[nodiscard]] bool scanning() const { return m_scan != nullptr; }

    // Called between the slices of a blocking refresh(), e.g. to service the USB drive.
    void set_yield(void (*yield)()) { m_yield = yield; }
    ~AppCatalog();
    AppCatalog();

    // Current directory: "" = root, else "dir/sub" (same form as AppBinaryEntry::path).
    [[nodiscard]] const std::string& cwd() const { return m_cwd; }
    [[nodiscard]] bool in_root() const { return m_cwd.empty(); }
    void set_directory(std::string path) { m_cwd = std::move(path); }
    // Move into the folder at `index` / to the parent. They only change cwd(): call
    // refresh() afterwards. enter() fails if `index` is not a folder or the path
    // would exceed kMaxPath; up() fails at the root and reports the folder left.
    bool enter(size_t index);
    bool up(std::string* left = nullptr);

    // Longest path the catalog builds (FatFs handles 255 UTF-16 units).
    static constexpr size_t kMaxPath = 240;

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
    struct Scan;                     // the open directory of a scan in progress
    void finish_scan();

    std::unique_ptr<Scan> m_scan;
    void (*m_yield)() = nullptr;
    std::vector<AppBinaryEntry> m_entries;
    std::string m_cwd;
    bool m_truncated = false;
};

} // namespace picoboot
