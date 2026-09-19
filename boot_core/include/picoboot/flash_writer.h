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
    kTooLarge, // file_size exceeds the app partition
};

enum class WriteResult {
    kOk,          // image now matches flash (possibly nothing needed writing)
    kReadFailed,  // the source could not be read
    kFlashFailed, // a flash critical section could not be entered (e.g. the other core did not park)
    kVerifyFailed, // read-back through XIP differs from what was written
};

// Sequential source of the new image (e.g. an SD-card file): fill `buf` with
// the next `len` bytes; return false on failure.
using ImageReader = bool (*)(void* ctx, uint8_t* buf, size_t len);

// Flash erase/program helper for the app partition at kAppFlashBase
// (boot_core/flash_layout.h). Every public entry point here is a
// self-contained operation with its own critical sections -- callers
// (app_manager) never need to wrap these in their own InterruptGuard.
class FlashWriter {
public:
    // Real, hard check: does `file_size` fit in `partition_size` bytes?
    // (partition_size = flash_layout::app_partition_size(PICO_FLASH_SIZE_BYTES)).
    // No architecture (RP2040-vs-RP2350) check is performed -- see
    // docs/architecture.md's documented limitation.
    [[nodiscard]] static FlashResult check_capacity(size_t file_size, size_t partition_size);

    // Streams a `size`-byte image from `read` into flash at `flash_base` in
    // 16 KiB blocks, so RAM use is one block regardless of image size (the
    // RP2040 has only 264 KB). Each 4 KiB sector of a block is compared
    // (memcmp, via the XIP window) with what is already flashed; only
    // differing sectors are erased and programmed, so re-loading an
    // identical image touches nothing. Sectors of a block are programmed in
    // one critical section (flash_safe_execute() when the other core is a
    // registered victim, else a plain InterruptGuard); progress callbacks and
    // file reads run *outside* critical sections.
    //
    // After kFlashFailed or a kReadFailed that follows a write, the partition
    // is partially written and MUST NOT be booted.
    // Byte offset (into the image) of the block that failed, after a failure.
    [[nodiscard]] static size_t failure_offset();

    [[nodiscard]] static WriteResult write_image(uint32_t flash_base, size_t size, ImageReader read,
                                                 void* read_ctx, const ProgressSink& sink);
};

} // namespace picoboot
