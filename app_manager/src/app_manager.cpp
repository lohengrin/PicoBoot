#include "picoboot/app_manager.h"

#include "picoboot/fastboot.h"
#include "picoboot/flash_layout.h"
#include "picoboot/image_check.h"

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>

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

    // The file's real size (the catalog's may be stale if the host changed it).
    struct stat st{};
    if (stat(entry.filename.c_str(), &st) != 0 || st.st_size <= 0) {
        printf("load: stat('%s') failed (errno %d)\n", entry.filename.c_str(), errno);
        return LoadResult::kReadFailed;
    }
    const long file_size = static_cast<long>(st.st_size);
    if (FlashWriter::check_capacity(static_cast<size_t>(file_size), partition_size) == FlashResult::kTooLarge) {
        return LoadResult::kTooLarge;
    }

    // Streamed from the card: RAM use is one 16 KiB block, not the image.
    FILE* file = fopen(entry.filename.c_str(), "rb");
    if (!file) {
        printf("load: fopen('%s') failed (errno %d)\n", entry.filename.c_str(), errno);
        return LoadResult::kReadFailed;
    }

    // Sanity-check the vector table (first bytes of the file) before touching flash.
    uint8_t head[kVectorTableOffset + 8];
    const size_t head_read = fread(head, 1, sizeof(head), file);
    if (head_read != sizeof(head)) {
        printf("load: reading the header of '%s' failed (%u of %u bytes, errno %d)\n", entry.filename.c_str(),
               static_cast<unsigned>(head_read), static_cast<unsigned>(sizeof(head)), errno);
        fclose(file);
        return LoadResult::kReadFailed;
    }
    if (!looks_like_app_image(std::span<const uint8_t>(head), kAppFlashBase, static_cast<uint32_t>(partition_size))) {
        fclose(file);
        return LoadResult::kInvalidImage;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        printf("load: rewinding '%s' failed (errno %d)\n", entry.filename.c_str(), errno);
        fclose(file);
        return LoadResult::kReadFailed;
    }

    const WriteResult result = FlashWriter::write_image(kAppFlashBase, static_cast<size_t>(file_size), read_from_file, file, sink);
    fclose(file);
    switch (result) {
        case WriteResult::kReadFailed:
            printf("load: reading '%s' failed while streaming (errno %d)\n", entry.filename.c_str(), errno);
            return LoadResult::kReadFailed;
        case WriteResult::kFlashFailed:
            printf("load: flash critical section failed at image offset 0x%X, error %d (-2 timeout: the other core did not park)\n",
                   static_cast<unsigned>(FlashWriter::failure_offset()), FlashWriter::failure_code());
            return LoadResult::kFlashFailed;
        case WriteResult::kVerifyFailed:
            printf("load: verify failed at image offset 0x%X (flash read-back differs from the file)\n",
                   static_cast<unsigned>(FlashWriter::failure_offset()));
            return LoadResult::kFlashFailed;
        case WriteResult::kOk: break;
    }

    m_config.last_run_binary = entry.filename;
    m_config.save();

    FastBoot::reboot_into_app();
}

} // namespace picoboot
