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

bool FlashWriter::compare_4k(uint32_t flash_base, std::span<const uint8_t> new_image) {
    const auto* current = reinterpret_cast<const uint8_t*>(flash_base);
    size_t offset = 0;
    while (offset < new_image.size()) {
        const size_t chunk = std::min(kFlashSectorSize, new_image.size() - offset);
        if (std::memcmp(current + offset, new_image.data() + offset, chunk) != 0) {
            return false;
        }
        offset += chunk;
    }
    return true;
}

namespace {

// Erase + program is done in blocks, each in its own critical section, so
// (a) progress can be reported between blocks (a UI running on the other
// core, or USB, gets to run in between), and (b) a second core that
// registered as a flash-safe-execute victim is only parked for one block at
// a time.
constexpr size_t kBlockBytes = 4 * kFlashSectorSize;

struct BlockParams {
    uint32_t flash_offset;
    const uint8_t* data;
    size_t len; // bytes of real data in this block
};

void do_erase_program_block(void* raw_params) {
    const auto* params = static_cast<const BlockParams*>(raw_params);
    const size_t erase_len = ((params->len + kFlashSectorSize - 1) / kFlashSectorSize) * kFlashSectorSize;
    flash_range_erase(params->flash_offset, erase_len);

    // flash_range_program needs page-aligned (256B) counts; the last page of
    // the image is padded through a scratch buffer rather than reading past
    // the caller's data.
    size_t offset = 0;
    while (offset < params->len) {
        const size_t remaining = params->len - offset;
        if (remaining >= FLASH_PAGE_SIZE) {
            flash_range_program(params->flash_offset + offset, params->data + offset, FLASH_PAGE_SIZE);
            offset += FLASH_PAGE_SIZE;
        } else {
            uint8_t page[FLASH_PAGE_SIZE];
            std::memcpy(page, params->data + offset, remaining);
            std::memset(page + remaining, 0xFF, FLASH_PAGE_SIZE - remaining);
            flash_range_program(params->flash_offset + offset, page, FLASH_PAGE_SIZE);
            offset += remaining;
        }
    }
}

bool run_block(BlockParams& params) {
    if (multicore_lockout_ready()) {
        return flash_safe_execute(do_erase_program_block, &params, 5000) == PICO_OK;
    }
    InterruptGuard guard;
    do_erase_program_block(&params);
    return true;
}

} // namespace

bool FlashWriter::erase_and_program(uint32_t flash_base, std::span<const uint8_t> new_image,
                                    const ProgressSink& sink) {
    const uint32_t flash_offset = flash_base - kFlashXipBase;
    const size_t total = new_image.size();

    size_t done = 0;
    while (done < total) {
        BlockParams block{flash_offset + static_cast<uint32_t>(done), new_image.data() + done,
                          std::min(kBlockBytes, total - done)};
        if (!run_block(block)) {
            return false;
        }
        done += block.len;
        sink.report(static_cast<float>(done) / static_cast<float>(total));
    }
    return true;
}

} // namespace picoboot
