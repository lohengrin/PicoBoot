#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace picoboot {

// Plain function-pointer + context callback (no std::function) so
// boot_core stays allocation-free -- matches the "keep bootloader
// footprint small" constraint. Called after each chunk written during
// erase_and_program(); `fraction` is in [0, 1].
struct ProgressSink {
    void (*on_progress)(void* ctx, float fraction) = nullptr;
    void* ctx = nullptr;

    void report(float fraction) const {
        if (on_progress) on_progress(ctx, fraction);
    }
};

enum class FlashResult {
    kOk,
    kTooLarge,     // file_size exceeds the app partition
    kIdentical,    // matched the currently-flashed image; nothing written
};

// Flash erase/program helper for the app partition at kAppFlashBase
// (boot_core/flash_layout.h). Every public entry point here is a
// self-contained operation with its own critical section -- callers
// (app_manager) never need to wrap these in their own InterruptGuard.
class FlashWriter {
public:
    // Real, hard check: does `file_size` fit in `partition_size` bytes?
    // (partition_size = flash_layout::app_partition_size(PICO_FLASH_SIZE_BYTES)).
    // No architecture (RP2040-vs-RP2350) check is performed -- see
    // docs/architecture.md's documented limitation.
    [[nodiscard]] static FlashResult check_capacity(size_t file_size, size_t partition_size);

    // 4KB-chunked memcmp of `new_image` against the bytes currently at
    // `flash_base` (read via the XIP memory-mapped pointer -- no special
    // API needed to read flash, only to erase/program it). The final
    // partial chunk is compared only over new_image's actual remaining
    // length, not padded to 4KB, so a same-content-but-currently-larger
    // old image still compares identical. Returns true if identical
    // (caller should skip programming and boot as-is).
    [[nodiscard]] static bool compare_4k(uint32_t flash_base, std::span<const uint8_t> new_image);

    // Erases exactly the sectors `new_image` occupies (rounded up to
    // kFlashSectorSize) and programs it in 16 KiB blocks, reporting progress
    // via `sink` between blocks. Each block runs in its own critical section:
    // flash_safe_execute() when the other core is a registered victim (see
    // pico_flash), else a plain InterruptGuard -- mirrors components/psram's
    // psram_init() idiom. Progress callbacks therefore run *outside* the
    // critical section.
    // Returns false if a flash critical section could not be entered (e.g.
    // the other core failed to park within the timeout) -- the partition
    // is then partially written and MUST NOT be booted.
    [[nodiscard]] static bool erase_and_program(uint32_t flash_base, std::span<const uint8_t> new_image,
                                                const ProgressSink& sink);
};

} // namespace picoboot
