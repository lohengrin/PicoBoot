#include "picoboot/storage_state.h"

#include "ff.h"
#include "diskio.h"

namespace picoboot {

namespace {
bool g_write_locked = false;
bool g_failed = false;
uint32_t g_epoch = 0;
} // namespace

void storage_set_write_lock(bool locked) { g_write_locked = locked; }
bool storage_write_locked() { return g_write_locked; }

void storage_set_failed(bool failed) { g_failed = failed; }
bool storage_failed() { return g_failed; }

void storage_note_remount() {
    g_failed = false;
    ++g_epoch;
}
uint32_t storage_epoch() { return g_epoch; }

bool storage_sync() { return disk_ioctl(0, CTRL_SYNC, nullptr) == RES_OK; }

} // namespace picoboot
