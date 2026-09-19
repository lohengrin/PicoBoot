#include "picoboot/flash_writer.h"

#include "picoboot/critical_section.h"
#include "picoboot/flash_layout.h"

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/multicore.h"

#include <algorithm>
#include <cstring>

namespace picoboot {

FlashResult FlashWriter::check_capacity(size_t file_size, size_t partition_size) {
    return file_size <= partition_size ? FlashResult::kOk : FlashResult::kTooLarge;
}

namespace {

constexpr size_t kBlockBytes = 4 * kFlashSectorSize;
uint8_t g_block[kBlockBytes]; // the only RAM the image streaming needs
size_t g_failure_offset = 0;
int g_failure_code = 0;

struct BlockParams {
    uint32_t flash_offset;  // of the block
    const uint8_t* data;
    size_t len;             // bytes of real data in this block
    uint32_t sector_mask;   // bit i: sector i of the block differs and must be rewritten
};

void do_program_block(void* raw_params) {
    const auto* params = static_cast<const BlockParams*>(raw_params);
    for (size_t sector = 0; sector * kFlashSectorSize < params->len; ++sector) {
        if (!(params->sector_mask & (1u << sector))) continue;

        const size_t start = sector * kFlashSectorSize;
        const size_t sector_len = std::min(kFlashSectorSize, params->len - start);
        flash_range_erase(params->flash_offset + start, kFlashSectorSize);

        // flash_range_program needs page-aligned (256B) counts; the last page
        // of the image is padded through a scratch buffer rather than reading
        // past the block's data.
        size_t offset = 0;
        while (offset < sector_len) {
            const size_t remaining = sector_len - offset;
            if (remaining >= FLASH_PAGE_SIZE) {
                flash_range_program(params->flash_offset + start + offset, params->data + start + offset,
                                    FLASH_PAGE_SIZE);
                offset += FLASH_PAGE_SIZE;
            } else {
                uint8_t page[FLASH_PAGE_SIZE];
                std::memcpy(page, params->data + start + offset, remaining);
                std::memset(page + remaining, 0xFF, FLASH_PAGE_SIZE - remaining);
                flash_range_program(params->flash_offset + start + offset, page, FLASH_PAGE_SIZE);
                offset += remaining;
            }
        }
    }
}

bool run_block(BlockParams& params) {
    if (multicore_lockout_ready()) {
        g_failure_code = flash_safe_execute(do_program_block, &params, 5000);
        return g_failure_code == PICO_OK;
    }
    InterruptGuard guard;
    do_program_block(&params);
    return true;
}

} // namespace

size_t FlashWriter::failure_offset() { return g_failure_offset; }
int FlashWriter::failure_code() { return g_failure_code; }

WriteResult FlashWriter::write_image(uint32_t flash_base, size_t size, ImageReader read, void* read_ctx,
                                     const ProgressSink& sink) {
    const uint32_t flash_offset = flash_base - kFlashXipBase;
    const auto* flash = reinterpret_cast<const uint8_t*>(flash_base); // XIP window
    bool wrote = false;

    size_t done = 0;
    while (done < size) {
        const size_t len = std::min(kBlockBytes, size - done);
        if (!read(read_ctx, g_block, len)) {
            g_failure_offset = done;
            return wrote ? WriteResult::kFlashFailed : WriteResult::kReadFailed;
        }

        uint32_t mask = 0;
        for (size_t sector = 0; sector * kFlashSectorSize < len; ++sector) {
            const size_t start = sector * kFlashSectorSize;
            const size_t sector_len = std::min(kFlashSectorSize, len - start);
            if (std::memcmp(flash + done + start, g_block + start, sector_len) != 0) mask |= 1u << sector;
        }

        if (mask != 0) {
            BlockParams block{flash_offset + static_cast<uint32_t>(done), g_block, len, mask};
            wrote = true;
            g_failure_offset = done;
            if (!run_block(block)) return WriteResult::kFlashFailed;
            // Read back through the XIP window: never boot an image that did not land.
            if (std::memcmp(flash + done, g_block, len) != 0) return WriteResult::kVerifyFailed;
        }

        done += len;
        sink.report(static_cast<float>(done) / static_cast<float>(size));
    }
    return WriteResult::kOk;
}

} // namespace picoboot
