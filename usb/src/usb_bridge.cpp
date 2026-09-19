#include "picoboot/usb_bridge.h"

#include "pico_toolset/usb_composite.h"

#include "ff.h"
#include "diskio.h"

namespace picoboot {

namespace {

const pico_toolset::SdCard* g_sd = nullptr;
void (*g_bus_hook)() = nullptr;

void prepare_bus() {
    if (g_bus_hook) g_bus_hook();
}

bool ready(void*) { return g_sd && g_sd->is_mounted(); }

uint32_t block_count(void*) {
    prepare_bus();
    LBA_t sectors = 0;
    return disk_ioctl(0, GET_SECTOR_COUNT, &sectors) == RES_OK ? static_cast<uint32_t>(sectors) : 0;
}

bool read(void*, uint32_t lba, uint8_t* buf, uint32_t count) {
    prepare_bus();
    return disk_read(0, buf, lba, count) == RES_OK;
}

bool write(void*, uint32_t lba, const uint8_t* buf, uint32_t count) {
    prepare_bus();
    return disk_write(0, buf, lba, count) == RES_OK;
}

} // namespace

void usb_bridge_init(const pico_toolset::SdCard& sd_card) {
    g_sd = &sd_card;
    pico_toolset::UsbCompositeConfig cfg;
    cfg.block_device = {nullptr, ready, block_count, read, write};
    cfg.manufacturer = "Lohengrin";
    cfg.product = "PicoBoot";
    cfg.msc_vendor = "PicoBoot";
    cfg.msc_product = "SD card";
    pico_toolset::usb_composite_init(cfg);
}

void usb_bridge_set_bus_hook(void (*hook)()) { g_bus_hook = hook; }

void usb_bridge_task() { pico_toolset::usb_composite_task(); }

} // namespace picoboot
