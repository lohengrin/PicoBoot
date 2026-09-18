#pragma once

#include <cstdint>
#include <string>

namespace picoboot {

// picoboot.cfg on the SD card root: last-run binary name and auto-boot
// timeout in seconds (0 = disabled). Simple line-based "key=value" text
// format -- read/written via stdio (fopen/fgets/fprintf) over the
// PICO_TOOLSET_SDCARD_STDIO shim, since pico_toolset::SdCard itself is
// read-only.
class PicoBootConfig {
public:
    static constexpr uint32_t kDefaultAutoBootTimeoutS = 30;
    static constexpr const char* kFilename = "picoboot.cfg";

    // Populates fields from picoboot.cfg if present; leaves defaults
    // (empty last_run_binary, 30s timeout) untouched on any read failure
    // (missing file, unmounted card, malformed content) -- a missing or
    // bad config is not fatal, it's "use the defaults".
    bool load();

    // Overwrites picoboot.cfg with the current fields. Returns false if
    // the card isn't writable (unmounted, or a stdio error) -- callers
    // should treat that as non-fatal (booting still proceeds; only the
    // "remember last run" convenience is lost for this session).
    bool save() const;

    std::string last_run_binary;
    uint32_t auto_boot_timeout_s = kDefaultAutoBootTimeoutS;
};

} // namespace picoboot
