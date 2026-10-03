#include "picoboot/app_manager.h"

#include "picoboot/fastboot.h"
#include "picoboot/flash_layout.h"
#include "picoboot/image_check.h"
#include "picoboot/memory_probe.h"
#include "picoboot/storage_state.h"

#include "ff.h"
#include "diskio.h"

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>

namespace picoboot {

bool AppManager::refresh() {
    m_sd_card.init(m_sd_config);
    storage_note_remount(); // clears a failed state; the USB drive re-reads the medium
    if (m_sd_card.is_mounted()) {
        m_config.load();
        if (!m_dir_from_config) {
            // Start where the last app came from (the catalog falls back to the root
            // if that folder is gone).
            m_dir_from_config = true;
            const size_t slash = m_config.last_run_binary.rfind('/');
            m_catalog.set_directory(slash == std::string::npos ? std::string()
                                                               : m_config.last_run_binary.substr(0, slash));
        }
    }
    m_catalog.refresh(m_sd_card);
    return m_sd_card.is_mounted();
}

std::string AppManager::next_screenshot_path() {
    if (!card_present()) return {};
    // Continue after the last number used; the first call after boot probes from 1.
    for (unsigned n = m_screenshot_counter + 1; n < 10000; ++n) {
        char path[32];
        snprintf(path, sizeof(path), "screenshot_%04u.bmp", n);
        struct stat st{};
        if (stat(path, &st) != 0) {
            m_screenshot_counter = n;
            return path;
        }
    }
    return {};
}

bool AppManager::browse_into(size_t index) {
    if (!m_catalog.enter(index)) return false;
    refresh(); // remounts: the host may have changed the volume over USB
    return true;
}

void AppManager::browse_root() {
    m_catalog.set_directory({});
    refresh();
}

bool AppManager::browse_up(std::string* left) {
    if (!m_catalog.up(left)) return false;
    refresh();
    return true;
}

bool AppManager::card_present() const { return m_sd_card.is_mounted() && !storage_failed(); }

CardStatus AppManager::card_status() const {
    if (card_present()) return CardStatus::kReady;
    if (m_sd_card.is_mounted()) return CardStatus::kNoCard; // mounted earlier, stopped answering
    switch (m_sd_card.last_mount_result()) {
        case FR_DISK_ERR:
        case FR_NOT_READY:
        case -1: return CardStatus::kNoCard; // -1: never mounted
        case FR_NO_FILESYSTEM: return CardStatus::kUnsupportedFilesystem;
        default: return CardStatus::kError;
    }
}

std::string AppManager::card_message() const {
    switch (card_status()) {
        case CardStatus::kReady: return "uSD card ready";
        case CardStatus::kNoCard:
            return m_sd_card.is_mounted() ? "uSD card stopped responding (Refresh to remount)" : "no uSD card";
        case CardStatus::kUnsupportedFilesystem: return "uSD card has no FAT/exFAT volume (GPT or unformatted?)";
        case CardStatus::kError: return "uSD card error (mount result " + std::to_string(m_sd_card.last_mount_result()) + ")";
    }
    return "uSD card error";
}

std::string AppManager::describe_storage() const {
    char text[400];
    LBA_t sectors = 0;
    const bool have_size = m_sd_card.is_mounted() && disk_ioctl(0, GET_SECTOR_COUNT, &sectors) == RES_OK;
    int n = snprintf(text, sizeof(text),
                     "card:     %s\n"
                     "mount:    FatFs result %d, %s\n",
                     card_message().c_str(), m_sd_card.last_mount_result(),
                     m_sd_card.used_native_spi() ? "hardware SPI" : "PIO SPI");
    if (have_size) {
        n += snprintf(text + n, sizeof(text) - n, "capacity: %lu sectors (%lu MiB)\n",
                      static_cast<unsigned long>(sectors), static_cast<unsigned long>(sectors / 2048));
    }
    n += snprintf(text + n, sizeof(text) - n, "files:    %u listed%s\n", static_cast<unsigned>(m_catalog.count()),
                  m_catalog.truncated() ? " (more on the card)" : "");
    n += snprintf(text + n, sizeof(text) - n,
                  "usb:      write lock %s, card %s\n"
                  "stack:    %u of %u bytes never used\n"
                  "heap:     %u bytes free\n",
                  storage_write_locked() ? "ON" : "off", storage_failed() ? "FAILED" : "ok",
                  static_cast<unsigned>(stack_unused()), static_cast<unsigned>(stack_total()),
                  static_cast<unsigned>(heap_free()));
    return text;
}

namespace {

bool read_from_file(void* ctx, uint8_t* buf, size_t len) {
    return fread(buf, 1, len, static_cast<FILE*>(ctx)) == len;
}

std::string format(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string format(const char* fmt, ...) {
    char text[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    return text;
}

uint8_t g_head[kImageHeadBytes];

} // namespace

LoadResult AppManager::fail(LoadResult result, std::string short_text, std::string long_text) {
    m_error_short = std::move(short_text);
    m_error_long = std::move(long_text);
    return result;
}

namespace {
// The USB host must not write to the card while a file is streamed from it.
struct WriteLockGuard {
    WriteLockGuard() { storage_set_write_lock(true); }
    ~WriteLockGuard() { storage_set_write_lock(false); }
};
} // namespace

LoadResult AppManager::load_and_boot(const AppBinaryEntry& entry, const ProgressSink& sink) {
    WriteLockGuard write_lock;
    const size_t partition_size = app_partition_size(PICO_FLASH_SIZE_BYTES);
    const char* name = entry.path.c_str();

    // The file's real size (the catalog's may be stale if the host changed it).
    struct stat st{};
    if (stat(name, &st) != 0 || st.st_size <= 0) {
        return fail(LoadResult::kReadFailed, format("cannot read '%s'", name),
                    format("Could not read '%s' from the SD card: stat failed (errno %d).", name, errno));
    }
    const size_t file_size = static_cast<size_t>(st.st_size);
    if (FlashWriter::check_capacity(file_size, partition_size) == FlashResult::kTooLarge) {
        return fail(LoadResult::kTooLarge, format("'%s' is larger than the app partition", name),
                    format("'%s' (%lu bytes) does not fit in the application partition (%lu bytes).", name,
                           static_cast<unsigned long>(file_size), static_cast<unsigned long>(partition_size)));
    }

    // Streamed from the card: RAM use is one 16 KiB block, not the image.
    FILE* file = fopen(name, "rb");
    if (!file) {
        return fail(LoadResult::kReadFailed, format("cannot open '%s'", name),
                    format("Could not open '%s' (errno %d).", name, errno));
    }

    // Inspect the file itself before touching flash: which chip it was built
    // for and where it was linked.
    const size_t head_len = file_size < kImageHeadBytes ? file_size : kImageHeadBytes;
    if (fread(g_head, 1, head_len, file) != head_len) {
        fclose(file);
        return fail(LoadResult::kReadFailed, format("cannot read the header of '%s'", name),
                    format("Could not read the first %u bytes of '%s' (errno %d).", static_cast<unsigned>(head_len),
                           name, errno));
    }
    const ImageInfo info =
        inspect_image(std::span<const uint8_t>(g_head, head_len), kAppFlashBase, static_cast<uint32_t>(partition_size));
    if (info.result != ImageCheck::kOk) {
        fclose(file);
        switch (info.result) {
            case ImageCheck::kTooSmall:
                return fail(LoadResult::kInvalidImage, format("'%s' is too small to be an app", name),
                            format("'%s' is too small to be an RP2040 or RP2350 application image.", name));
            case ImageCheck::kUnknownChip:
                return fail(LoadResult::kInvalidImage, "not an RP2040/RP2350 app image",
                            format("'%s' is not a recognisable RP2040 or RP2350 application image (no RP2040 boot "
                                   "stage and no RP2350 image definition in its first 4 KiB).", name));
            case ImageCheck::kWrongChip:
                return fail(LoadResult::kInvalidImage,
                            format("built for %s, this board is %s", chip_name(info.chip), chip_name(kBoardChip)),
                            format("'%s' was built for the %s but this board is an %s: it cannot run here.", name,
                                   chip_name(info.chip), chip_name(kBoardChip)));
            case ImageCheck::kRiscV:
                return fail(LoadResult::kInvalidImage, "RISC-V image; this bootloader runs Arm",
                            format("'%s' is an RP2350 RISC-V image; this bootloader starts applications on the Arm "
                                   "cores.", name));
            case ImageCheck::kBadVectors:
                return fail(LoadResult::kInvalidImage, "invalid vector table (not a flash app)",
                            format("'%s' has an invalid vector table (stack pointer not in SRAM, or reset vector not "
                                   "in flash): it is not a flash application image, or it was linked for another "
                                   "address.", name));
            case ImageCheck::kUnsupportedLayout:
                return fail(LoadResult::kInvalidImage, "linked for 0x10000000: unsupported on RP2040",
                            format("'%s' was linked at 0x10000000 (a normal build). The RP2040 cannot map flash to "
                                   "another address, so it only runs apps linked for the application partition at "
                                   "0x%08lX. Rebuild it with picoboot_set_app_flash_region() (see the README).", name,
                                   static_cast<unsigned long>(kAppFlashBase)));
            case ImageCheck::kOk: break;
        }
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return fail(LoadResult::kReadFailed, format("cannot rewind '%s'", name),
                    format("Could not rewind '%s' (errno %d).", name, errno));
    }

    const WriteResult result = FlashWriter::write_image(kAppFlashBase, file_size, read_from_file, file, sink);
    fclose(file);
    switch (result) {
        case WriteResult::kReadFailed:
            return fail(LoadResult::kReadFailed, format("reading '%s' failed", name),
                        format("Reading '%s' failed while streaming it (errno %d); nothing was booted.", name, errno));
        case WriteResult::kFlashFailed:
            return fail(LoadResult::kFlashFailed, "flashing failed; partition not bootable",
                        format("Flashing '%s' failed at image offset 0x%X: a flash critical section could not be "
                               "entered (error %d; -2 means the other core did not park). The application partition is "
                               "not bootable.", name, static_cast<unsigned>(FlashWriter::failure_offset()),
                               FlashWriter::failure_code()));
        case WriteResult::kVerifyFailed:
            return fail(LoadResult::kFlashFailed, "flash verify failed; partition not bootable",
                        format("Flashing '%s' failed at image offset 0x%X: the flash read-back differs from the file. "
                               "The application partition is not bootable.", name,
                               static_cast<unsigned>(FlashWriter::failure_offset())));
        case WriteResult::kReadFailedAfterWrite:
            return fail(LoadResult::kFlashFailed, "card read failed mid-load; partition not bootable",
                        format("Reading '%s' from the SD card failed after flashing had begun (errno %d: card removed "
                               "or I/O error). The application partition is half written and not bootable; load "
                               "an app again.", name, errno));
        case WriteResult::kOk: break;
    }

    // Remember the app -- but only write the file when something changed: fewer
    // card writes, and less chance of touching the FAT while a host has it mounted.
    if (m_config.last_run_binary != entry.path) {
        m_config.last_run_binary = entry.path;
        if (!m_config.save()) {
            printf("warning: could not save picoboot.cfg (errno %d); auto-boot will not remember '%s'\n", errno, name);
        }
    }
    storage_sync(); // let the card finish any write before the reset

    if (info.layout == ImageLayout::kFlashBase) {
        FastBoot::reboot_into_app_remapped(); // RP2350 only: inspect_image() rejects it elsewhere
    }
    FastBoot::reboot_into_app();
}

} // namespace picoboot
