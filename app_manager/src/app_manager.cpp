#include "picoboot/app_manager.h"

#include "picoboot/fastboot.h"
#include "picoboot/flash_layout.h"

namespace picoboot {

LoadResult AppManager::load_and_boot(const AppBinaryEntry& entry, const ProgressSink& sink) {
    const size_t partition_size = app_partition_size(PICO_FLASH_SIZE_BYTES);

    if (FlashWriter::check_capacity(entry.size_bytes, partition_size) == FlashResult::kTooLarge) {
        return LoadResult::kTooLarge;
    }

    const std::vector<uint8_t> image = m_sd_card.read_file(entry.filename);
    if (image.empty()) {
        return LoadResult::kReadFailed;
    }

    const std::span<const uint8_t> image_span(image);
    if (!FlashWriter::compare_4k(kAppFlashBase, image_span)) {
        FlashWriter::erase_and_program(kAppFlashBase, image_span, sink);
    }

    m_config.last_run_binary = entry.filename;
    m_config.save();

    FastBoot::reboot_into_app();
}

} // namespace picoboot
