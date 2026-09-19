#include "picoboot/app_manager.h"

#include "picoboot/fastboot.h"
#include "picoboot/flash_layout.h"

namespace picoboot {

bool AppManager::refresh() {
    m_sd_card.init(m_sd_config);
    m_catalog.refresh(m_sd_card);
    if (m_sd_card.is_mounted()) {
        m_config.load();
    }
    return m_sd_card.is_mounted();
}

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
        if (!FlashWriter::erase_and_program(kAppFlashBase, image_span, sink)) {
            return LoadResult::kFlashFailed;
        }
    }

    m_config.last_run_binary = entry.filename;
    m_config.save();

    FastBoot::reboot_into_app();
}

} // namespace picoboot
