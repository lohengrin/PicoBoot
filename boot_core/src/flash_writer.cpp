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

struct EraseProgramParams {
    uint32_t flash_offset;
    std::span<const uint8_t> new_image;
    const ProgressSink* sink;
};

void do_erase_program(void* raw_params) {
    auto* params = static_cast<EraseProgramParams*>(raw_params);
    const size_t total = params->new_image.size();

    const size_t erase_len = ((total + kFlashSectorSize - 1) / kFlashSectorSize) * kFlashSectorSize;
    flash_range_erase(params->flash_offset, erase_len);

    // flash_range_program requires a page-aligned (256B) count; new_image
    // itself is exactly `total` bytes with no guarantee of page alignment,
    // so the last page is padded through a local scratch buffer rather
    // than reading past new_image's end directly.
    size_t offset = 0;
    size_t next_progress_report = kFlashSectorSize;
    while (offset < total) {
        const size_t remaining = total - offset;
        if (remaining >= FLASH_PAGE_SIZE) {
            flash_range_program(params->flash_offset + offset, params->new_image.data() + offset,
                                 FLASH_PAGE_SIZE);
            offset += FLASH_PAGE_SIZE;
        } else {
            uint8_t page[FLASH_PAGE_SIZE];
            std::memcpy(page, params->new_image.data() + offset, remaining);
            std::memset(page + remaining, 0xFF, FLASH_PAGE_SIZE - remaining);
            flash_range_program(params->flash_offset + offset, page, FLASH_PAGE_SIZE);
            offset += remaining;
        }
        if (params->sink && (offset >= next_progress_report || offset >= total)) {
            params->sink->report(static_cast<float>(offset) / static_cast<float>(total));
            next_progress_report += kFlashSectorSize;
        }
    }
}

} // namespace

void FlashWriter::erase_and_program(uint32_t flash_base, std::span<const uint8_t> new_image,
                                     const ProgressSink& sink) {
    EraseProgramParams params{flash_base - kFlashXipBase, new_image, &sink};

    if (multicore_lockout_ready()) {
        flash_safe_execute(do_erase_program, &params, 5000);
    } else {
        InterruptGuard guard;
        do_erase_program(&params);
    }
}

} // namespace picoboot
