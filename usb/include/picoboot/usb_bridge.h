#pragma once

#include "pico_toolset/sdcard.h"

namespace picoboot {

// Starts the CDC+MSC composite device (pico_toolset_usb_composite) with
// the SD card's raw sectors as the mass-storage backend, and routes stdio
// over the CDC port. The host sees the same FAT volume the bootloader's own
// FatFs mount reads; coherency is manual by design -- after a host writes,
// the UI's "refresh" remounts (see main loop) rather than any live sync.
void usb_bridge_init(const pico_toolset::SdCard& sd_card);

// Optional: called before every raw SD access made on behalf of the USB host
// (capacity, read, write). Boards whose SD card shares a bus with other
// devices use it to put the bus back in the card's configuration -- the host
// enumerates and reads the capacity within milliseconds of startup, while
// other drivers may still be initialising.
void usb_bridge_set_bus_hook(void (*hook)());

// Pump the USB stack (also pumped by stdio input waits).
void usb_bridge_task();

} // namespace picoboot
