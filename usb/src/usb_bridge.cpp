#include "picoboot/usb_bridge.h"

#include "picoboot/storage_state.h"

#include "pico_toolset/usb_composite.h"

#include "ff.h"
#include "diskio.h"

#include "pico/stdlib.h"

namespace picoboot {

namespace {

const pico_toolset::SdCard* g_sd = nullptr;
void (*g_bus_hook)() = nullptr;

uint32_t g_seen_epoch = 0;
uint32_t g_known_blocks = 0;   // capacity at the last probe
uint32_t g_last_probe_ms = 0;
int g_consecutive_failures = 0;
bool g_last_ready = false;      // what the host was last told
bool g_saw_failure = false;     // the card stopped answering since the last remount
constexpr int kFailuresBeforeGone = 3;
constexpr uint32_t kProbeIntervalMs = 1000;

void prepare_bus() {
    if (g_bus_hook) g_bus_hook();
}

void note_io(bool ok) {
    if (ok) {
        g_consecutive_failures = 0;
    } else if (++g_consecutive_failures >= kFailuresBeforeGone) {
        storage_set_failed(true); // stays until a refresh remounts the card
        g_saw_failure = true;
    }
}

// Asks the card for its capacity (a command it cannot answer when removed).
bool read_capacity(uint32_t& blocks) {
    prepare_bus();
    LBA_t sectors = 0;
    if (disk_ioctl(0, GET_SECTOR_COUNT, &sectors) != RES_OK) return false;
    blocks = static_cast<uint32_t>(sectors);
    return true;
}

// "Is the medium there?" -- the mounted flag alone never notices a removed
// card. Probed at most once a second (hosts poll TEST UNIT READY constantly).
bool ready(void*) {
    bool result = g_sd && g_sd->is_mounted() && !storage_failed();

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (result && now - g_last_probe_ms >= kProbeIntervalMs) {
        g_last_probe_ms = now;
        uint32_t blocks = 0;
        if (!read_capacity(blocks)) {
            note_io(false);
            result = !storage_failed();
        } else {
            note_io(true);
            if (g_known_blocks != 0 && blocks != g_known_blocks) {
                pico_toolset::usb_composite_media_changed(); // a different card
            }
            g_known_blocks = blocks;
        }
    }
    g_last_ready = result;
    return result;
}

uint32_t block_count(void*) {
    uint32_t blocks = 0;
    if (!read_capacity(blocks)) {
        note_io(false);
        return 0;
    }
    g_known_blocks = blocks;
    return blocks;
}

bool read(void*, uint32_t lba, uint8_t* buf, uint32_t count) {
    prepare_bus();
    const bool ok = disk_read(0, buf, lba, count) == RES_OK;
    note_io(ok);
    return ok;
}

bool write(void*, uint32_t lba, const uint8_t* buf, uint32_t count) {
    prepare_bus();
    const bool ok = disk_write(0, buf, lba, count) == RES_OK;
    note_io(ok);
    return ok;
}

// SYNCHRONIZE CACHE / eject: wait until the card has finished programming.
bool sync(void*) {
    prepare_bus();
    return storage_sync();
}

// Read-only for the host while a load streams a file from the card.
bool writable(void*) { return !storage_write_locked(); }

} // namespace

void usb_bridge_init(const pico_toolset::SdCard& sd_card) {
    g_sd = &sd_card;
    g_seen_epoch = storage_epoch();
    pico_toolset::UsbCompositeConfig cfg;
    cfg.block_device = {nullptr, ready, block_count, read, write, sync, writable};
    cfg.manufacturer = "Lohengrin";
    cfg.product = "PicoBoot";
    cfg.msc_vendor = "PicoBoot";
    cfg.msc_product = "SD card";
    pico_toolset::usb_composite_init(cfg);
}

void usb_bridge_set_bus_hook(void (*hook)()) { g_bus_hook = hook; }

void usb_bridge_task() {
    // A remount happened (Refresh). Tell the host the medium may have changed
    // only if it really might have -- the card was gone or stopped answering, it
    // appeared or vanished, or its capacity differs -- so pressing Refresh
    // during a host copy does not disturb it.
    if (const uint32_t epoch = storage_epoch(); epoch != g_seen_epoch) {
        g_seen_epoch = epoch;
        g_consecutive_failures = 0;
        g_last_probe_ms = 0;

        const bool now_ready = g_sd && g_sd->is_mounted();
        uint32_t blocks = 0;
        const bool have_size = now_ready && read_capacity(blocks);
        // A host eject hides the medium until told otherwise: Refresh presents it again.
        const bool changed = g_saw_failure || pico_toolset::usb_composite_ejected() || now_ready != g_last_ready ||
                             (have_size && g_known_blocks != 0 && blocks != g_known_blocks);
        g_saw_failure = false;
        g_last_ready = now_ready;
        if (have_size) g_known_blocks = blocks;
        if (changed) pico_toolset::usb_composite_media_changed();
    }
    pico_toolset::usb_composite_task();
}

} // namespace picoboot
