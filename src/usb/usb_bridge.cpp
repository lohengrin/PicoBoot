#include "picoboot/usb_bridge.h"

#include "picoboot/storage_state.h"

#include "pico_toolset/usb_composite.h"

#include "ff.h"
#include "diskio.h"

#include "pico/stdlib.h"

#include <cstdio>
#include <cstring>

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

// Read-ahead. A sequential host read is serialised with the card (card read, then
// USB transfer, then the next request). While the USB hardware is still sending
// one chunk the CPU is idle, so usb_bridge_task() reads the *next* chunk from the
// card into this buffer and the following READ(10) is served from RAM.
// Only sequential streams are prefetched; any write, flush, remount or a stale
// entry (the bootloader itself may have modified the card) drops it.
constexpr uint32_t kRaSectors = 32; // = CFG_TUD_MSC_EP_BUFSIZE / 512 (the largest READ(10) chunk)
constexpr uint32_t kRaMaxAgeMs = 1000;
alignas(4) uint8_t g_ra_buf[kRaSectors * 512];
uint32_t g_ra_lba = 0;
uint32_t g_ra_count = 0;        // 0 = nothing cached
uint32_t g_ra_filled_ms = 0;
uint32_t g_ra_want_lba = 0;     // prefetch scheduled for the main loop
uint32_t g_ra_want_count = 0;   // 0 = none
uint32_t g_last_read_end = UINT32_MAX; // sector after the previous READ(10), to spot sequential streams

void ra_invalidate() {
    g_ra_count = 0;
    g_ra_want_count = 0;
    g_last_read_end = UINT32_MAX;
}

// Write-behind. TinyUSB re-arms its endpoint buffer as soon as WRITE(10) returns, so
// a card write done inside the callback keeps the USB wire idle (and vice versa).
// Instead the callback copies the chunk here and returns; usb_bridge_task() commits
// it to the card right after the stack returns, while the next chunk is arriving.
// A chunk therefore sits in RAM only for a few ms, and every path that could observe
// the card (reads, SYNCHRONIZE CACHE, eject, remount) commits it first. A failed
// commit is reported on the next WRITE(10) or SYNCHRONIZE CACHE.
constexpr uint32_t kWbSectors = 32;
alignas(4) uint8_t g_wb_buf[kWbSectors * 512];
uint32_t g_wb_lba = 0;
uint32_t g_wb_count = 0; // 0 = nothing pending
bool g_wb_error = false;

// Throughput instrumentation (-DPICOBOOT_USB_STATS=ON): time spent inside the SD
// backend vs. the wall time of a burst of host I/O, how often the USB stack is
// serviced, and read-ahead / write-behind activity, printed over stdio (CDC) once
// the host goes quiet. Compiled out (empty inline stubs) otherwise.
#ifdef PICOBOOT_USB_STATS
struct IoStats {
    uint32_t calls = 0;
    uint64_t bytes = 0;
    uint64_t sd_us = 0;
};
struct GapStats {
    uint32_t n = 0;
    uint64_t sum_us = 0;
    uint32_t max_us = 0;
    uint32_t over_2ms = 0;
    uint32_t over_10ms = 0;
    void add(uint64_t us) {
        ++n;
        sum_us += us;
        if (us > max_us) max_us = static_cast<uint32_t>(us);
        if (us > 2000) ++over_2ms;
        if (us > 10000) ++over_10ms;
    }
};
IoStats g_rd, g_wr;
GapStats g_task_gap; // between consecutive usb_bridge_task() calls
GapStats g_io_gap;   // between the end of one SD call and the start of the next
uint64_t g_burst_first_us = 0; // 0 = no burst in progress
uint64_t g_burst_last_us = 0;
uint64_t g_last_task_us = 0;
uint64_t g_last_io_end_us = 0;
uint64_t g_sync_us = 0;
uint64_t g_tud_us = 0; // time inside the USB stack itself (includes the SD callbacks)
uint32_t g_ra_hits = 0, g_ra_fetches = 0, g_wb_queued = 0, g_wb_commits = 0;
uint64_t g_ra_sd_us = 0, g_wb_sd_us = 0;
constexpr uint32_t kBurstIdleUs = 500'000;

inline uint64_t stat_now() { return time_us_64(); }

// One host READ(10)/WRITE(10) callback that started at t0.
void stat_io(bool is_write, uint32_t count, uint64_t t0) {
    const uint64_t end = time_us_64();
    if (g_last_io_end_us != 0 && g_burst_first_us != 0) g_io_gap.add(t0 - g_last_io_end_us);
    g_last_io_end_us = end;
    IoStats& s = is_write ? g_wr : g_rd;
    s.calls++;
    s.bytes += static_cast<uint64_t>(count) * 512;
    s.sd_us += end - t0;
    if (g_burst_first_us == 0) g_burst_first_us = t0;
    g_burst_last_us = end;
}
void stat_sync(uint64_t t0) { g_sync_us += time_us_64() - t0; }
void stat_ra_hit() { ++g_ra_hits; }
void stat_ra_fetch(uint64_t t0) {
    ++g_ra_fetches;
    g_ra_sd_us += time_us_64() - t0;
}
void stat_wb_queued() { ++g_wb_queued; }
void stat_wb_commit(uint64_t t0) {
    ++g_wb_commits;
    g_wb_sd_us += time_us_64() - t0;
}
// One usb_bridge_task() USB-stack call that started at t0.
void stat_task(uint64_t t0) {
    const uint64_t t1 = time_us_64();
    if (g_burst_first_us != 0) {
        if (g_last_task_us != 0) g_task_gap.add(t0 - g_last_task_us);
        g_tud_us += t1 - t0;
    }
    g_last_task_us = t1;
}

void stat_report_if_idle() {
    if (g_burst_first_us == 0 || time_us_64() - g_burst_last_us < kBurstIdleUs) return;
    const uint64_t wall = g_burst_last_us - g_burst_first_us;
    auto line = [wall](const char* name, const IoStats& s) {
        if (!s.calls) return;
        printf("[usbstat] %s: %lu calls, %llu KB, avg %llu B/call, sd %llu ms (%llu kB/s in sd), %llu%% of wall\n", name,
               (unsigned long)s.calls, (unsigned long long)(s.bytes / 1024), (unsigned long long)(s.bytes / s.calls),
               (unsigned long long)(s.sd_us / 1000), (unsigned long long)(s.bytes * 1000 / (s.sd_us ? s.sd_us : 1)),
               (unsigned long long)(s.sd_us * 100 / (wall ? wall : 1)));
    };
    auto gap = [](const char* name, const GapStats& g) {
        printf("[usbstat] %s: %lu, avg %llu us, max %lu us, >2ms %lu, >10ms %lu\n", name, (unsigned long)g.n,
               (unsigned long long)(g.n ? g.sum_us / g.n : 0), (unsigned long)g.max_us, (unsigned long)g.over_2ms,
               (unsigned long)g.over_10ms);
    };
    printf("[usbstat] burst wall %llu ms, sync %llu ms\n", (unsigned long long)(wall / 1000),
           (unsigned long long)(g_sync_us / 1000));
    line("read ", g_rd);
    line("write", g_wr);
    gap("task gap   ", g_task_gap);
    gap("io-to-io   ", g_io_gap);
    printf("[usbstat] write-behind: %lu queued, %lu commits, %llu ms\n", (unsigned long)g_wb_queued,
           (unsigned long)g_wb_commits, (unsigned long long)(g_wb_sd_us / 1000));
    printf("[usbstat] read-ahead: %lu hits, %lu prefetches, %llu ms\n", (unsigned long)g_ra_hits,
           (unsigned long)g_ra_fetches, (unsigned long long)(g_ra_sd_us / 1000));
    printf("[usbstat] tud_task total %llu ms (incl. sd %llu ms)\n", (unsigned long long)(g_tud_us / 1000),
           (unsigned long long)((g_rd.sd_us + g_wr.sd_us) / 1000));
    g_rd = {};
    g_wr = {};
    g_task_gap = {};
    g_io_gap = {};
    g_ra_hits = g_ra_fetches = g_wb_queued = g_wb_commits = 0;
    g_ra_sd_us = g_wb_sd_us = g_sync_us = g_tud_us = 0;
    g_last_io_end_us = 0;
    g_burst_first_us = 0;
}
#else
inline uint64_t stat_now() { return 0; }
inline void stat_io(bool, uint32_t, uint64_t) {}
inline void stat_sync(uint64_t) {}
inline void stat_ra_hit() {}
inline void stat_ra_fetch(uint64_t) {}
inline void stat_wb_queued() {}
inline void stat_wb_commit(uint64_t) {}
inline void stat_task(uint64_t) {}
inline void stat_report_if_idle() {}
#endif

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
                ra_invalidate();
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

bool wb_flush();

bool read(void*, uint32_t lba, uint8_t* buf, uint32_t count) {
    wb_flush(); // a read must see every write already acknowledged (a failure is reported by the next write/sync)
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    const uint64_t t0 = stat_now();

    if (g_ra_count != 0 && now_ms - g_ra_filled_ms > kRaMaxAgeMs) ra_invalidate();

    bool ok;
    if (g_ra_count != 0 && lba >= g_ra_lba && lba + count <= g_ra_lba + g_ra_count) {
        memcpy(buf, g_ra_buf + (lba - g_ra_lba) * 512, count * 512);
        stat_ra_hit();
        ok = true;
    } else {
        prepare_bus();
        ok = disk_read(0, buf, lba, count) == RES_OK;
        note_io(ok);
    }
    stat_io(false, count, t0);

    const uint32_t end = lba + count;
    const bool sequential = lba == g_last_read_end;
    g_last_read_end = ok ? end : UINT32_MAX;
    // Prefetch what follows once a sequential stream has used up the cached data.
    const bool cache_has_next = g_ra_count != 0 && end >= g_ra_lba && end < g_ra_lba + g_ra_count;
    if (ok && sequential && !cache_has_next && count <= kRaSectors &&
        (g_known_blocks == 0 || end + count <= g_known_blocks)) {
        g_ra_count = 0;
        g_ra_want_lba = end;
        g_ra_want_count = count;
    }
    return ok;
}

// Runs from the main loop, right after the READ(10) that scheduled it: the USB
// hardware is still transferring that chunk, so this overlaps with the wire time.
void ra_prefetch() {
    if (g_ra_want_count == 0) return;
    const uint32_t lba = g_ra_want_lba;
    const uint32_t count = g_ra_want_count;
    g_ra_want_count = 0;
    if (!g_sd || !g_sd->is_mounted() || storage_failed() || storage_write_locked()) return;

    prepare_bus();
    const uint64_t t0 = stat_now();
    const bool ok = disk_read(0, g_ra_buf, lba, count) == RES_OK;
    stat_ra_fetch(t0);
    note_io(ok);
    if (ok) {
        g_ra_lba = lba;
        g_ra_count = count;
        g_ra_filled_ms = to_ms_since_boot(get_absolute_time());
    }
}

// Commits the pending chunk to the card. False if that (or an earlier) commit failed.
bool wb_flush() {
    if (g_wb_count == 0) return !g_wb_error;
    const uint32_t lba = g_wb_lba;
    const uint32_t count = g_wb_count;
    g_wb_count = 0;
    prepare_bus();
    const uint64_t t0 = stat_now();
    const bool ok = disk_write(0, g_wb_buf, lba, count) == RES_OK;
    stat_wb_commit(t0);
    note_io(ok);
    if (!ok) g_wb_error = true;
    return !g_wb_error;
}

bool write(void*, uint32_t lba, const uint8_t* buf, uint32_t count) {
    // A commit that failed after its WRITE(10) was acknowledged is reported here.
    if (!wb_flush()) {
        g_wb_error = false;
        return false;
    }
    ra_invalidate();

    const uint64_t t0 = stat_now();
    if (count > kWbSectors) { // larger than the queue (not expected): write through
        prepare_bus();
        const bool ok = disk_write(0, buf, lba, count) == RES_OK;
        stat_io(true, count, t0);
        note_io(ok);
        return ok;
    }
    memcpy(g_wb_buf, buf, count * 512);
    g_wb_lba = lba;
    g_wb_count = count;
    stat_wb_queued();
    stat_io(true, count, t0);
    return true;
}

// SYNCHRONIZE CACHE / eject: wait until the card has finished programming.
bool sync(void*) {
    ra_invalidate();
    const uint64_t t0 = stat_now();
    bool ok = wb_flush();
    if (g_wb_error) {
        g_wb_error = false;
        ok = false;
    }
    prepare_bus();
    ok = storage_sync() && ok;
    stat_sync(t0);
    return ok;
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
        wb_flush(); // before the FatFs side remounts and reads the card
        ra_invalidate();
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
    const uint64_t t0 = stat_now();
    pico_toolset::usb_composite_task();
    stat_task(t0);
    wb_flush(); // a failure is held in g_wb_error until the host's next write/sync
    ra_prefetch();
    stat_report_if_idle();
}

} // namespace picoboot
