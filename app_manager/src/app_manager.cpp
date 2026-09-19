#include "picoboot/app_manager.h"

#include "picoboot/fastboot.h"
#include "picoboot/flash_layout.h"
#include "picoboot/image_check.h"

#include <cstdio>

namespace picoboot {

bool AppManager::refresh() {
    m_sd_card.init(m_sd_config);
    m_catalog.refresh(m_sd_card);
    if (m_sd_card.is_mounted()) {
        m_config.load();
    }
    return m_sd_card.is_mounted();
}

namespace {

bool read_from_file(void* ctx, uint8_t* buf, size_t len) {
    return fread(buf, 1, len, static_cast<FILE*>(ctx)) == len;
}

} // namespace

LoadResult AppManager::load_and_boot(const AppBinaryEntry& entry, const ProgressSink& sink) {
    const size_t partition_size = app_partition_size(PICO_FLASH_SIZE_BYTES);

    // Streamed from the card: RAM use is one 16 KiB block, not the image.
    FILE* file = fopen(entry.filename.c_str(), "rb");
    if (!file) {
        return LoadResult::kReadFailed;
    }
    // The file's real size (the catalog's may be stale if the host changed it).
    fseek(file, 0, SEEK_END);
    const long file_size = ftell(file);
    rewind(file);
    if (file_size <= 0) {
        fclose(file);
        return LoadResult::kReadFailed;
    }
    if (FlashWriter::check_capacity(static_cast<size_t>(file_size), partition_size) == FlashResult::kTooLarge) {
        fclose(file);
        return LoadResult::kTooLarge;
    }

    // Sanity-check the vector table (first bytes of the file) before touching flash.
    uint8_t head[kVectorTableOffset + 8];
    if (fread(head, 1, sizeof(head), file) != sizeof(head)) {
        fclose(file);
        return LoadResult::kReadFailed;
    }
    if (!looks_like_app_image(std::span<const uint8_t>(head), kAppFlashBase, static_cast<uint32_t>(partition_size))) {
        fclose(file);
        return LoadResult::kInvalidImage;
    }
    rewind(file);

    const WriteResult result = FlashWriter::write_image(kAppFlashBase, static_cast<size_t>(file_size), read_from_file, file, sink);
    fclose(file);
    switch (result) {
        case WriteResult::kReadFailed: return LoadResult::kReadFailed;
        case WriteResult::kFlashFailed: return LoadResult::kFlashFailed;
        case WriteResult::kOk: break;
    }

    m_config.last_run_binary = entry.filename;
    m_config.save();

    FastBoot::reboot_into_app();
}

} // namespace picoboot
