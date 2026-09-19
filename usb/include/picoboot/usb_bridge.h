#pragma once

#include "pico_toolset/sdcard.h"

namespace picoboot {

// Starts the CDC+MSC composite device (pico_toolset_usb_composite) with
// the SD card's raw sectors as the mass-storage backend, and routes stdio
// over the CDC port. The host sees the same FAT volume the bootloader's own
// FatFs mount reads; coherency is manual by design -- after a host writes,
// the UI's "refresh" remounts (see main loop) rather than any live sync.
void usb_bridge_init(const pico_toolset::SdCard& sd_card);

// Pump the USB stack (also pumped by stdio input waits).
void usb_bridge_task();

} // namespace picoboot
