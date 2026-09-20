#pragma once

#include <cstdint>

namespace picoboot {

// Small shared state about the SD card, so the layers that touch it (the
// loader, the UI, the USB drive) can coordinate without depending on each
// other.

// While locked, the USB host may read but not write: set for the duration of
// a load, when the loader streams a file from the card and a host writing to
// the same card would corrupt what gets flashed.
void storage_set_write_lock(bool locked);
[[nodiscard]] bool storage_write_locked();

// True once the card stopped answering (removed, or a run of I/O errors). Only
// a remount (refresh) clears it.
void storage_set_failed(bool failed);
[[nodiscard]] bool storage_failed();

// Called after every mount attempt; the USB drive uses the change to forget
// cached capacity and to tell the host the medium may have changed.
void storage_note_remount();
[[nodiscard]] uint32_t storage_epoch();

// Waits until the card has finished any internal write (SD CMD-level "busy"),
// so it is safe to reset or remove it. Returns false if the card did not answer.
bool storage_sync();

} // namespace picoboot
