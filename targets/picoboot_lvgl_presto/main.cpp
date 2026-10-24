#include "picoboot/app_manager.h"
#include "picoboot/board.h"
#include "picoboot/fastboot.h"
#include "picoboot/lvgl_ui.h"
#include "picoboot/memory_probe.h"
#include "picoboot/serial_ui.h"
#include "picoboot/usb_bridge.h"

#include "pico_toolset/ft6236.h"
#include "pico_toolset/ft6236_configs.h"
#include "pico_toolset/lvgl_display.h"
#include "pico_toolset/st7701.h"
#include "pico_toolset/st7701_configs.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <cstdio>

namespace {

constexpr int kWidth = 480;
constexpr int kHeight = 480;
constexpr size_t kDrawRows = 24;

// 8-bit palette scanout buffer (RGB332 colour table: 225 KB instead of 450 KB for RGB565).
// It must live in SRAM: PSRAM cannot feed the scanout DMA at pixel rate.
alignas(4) uint8_t g_framebuf[kWidth * kHeight];
alignas(64) uint16_t g_draw_buffer[kWidth * kDrawRows];

pico_toolset::St7701 g_lcd;

// The panel's PIO/DMA interrupts are served by the core that calls init(): core 1, so
// that core 0's flash writes (applications are programmed from here) cannot glitch the
// picture. Core 1 only ever runs RAM-resident code after init (the scanout handlers are in
// RAM; the idle loop must not be inlined into flash code).
void __no_inline_not_in_flash_func(core1_idle)() {
    while (true) __wfe();
}

void core1_main() {
    auto cfg = pico_toolset::configs::st7701::kPimoroniPrestoFullRes;
    cfg.framebuffer = g_framebuf;
    cfg.format = pico_toolset::St7701Format::Palette8;
    multicore_fifo_push_blocking(g_lcd.init(cfg) ? 1 : 0);
    core1_idle();
}

void pump_usb() { picoboot::usb_bridge_task(); }

} // namespace

int main() {
    picoboot::BootTag tag;
    bool from_app_request = false;
    if (picoboot::FastBoot::consume(tag)) {
        picoboot::FastBoot::boot_app_if_tagged(tag);
        from_app_request = (tag == picoboot::BootTag::kReenterBootloader);
    }

    picoboot::stack_paint(); // for the 'info' command's stack headroom
    stdio_init_all();

    // Everything that touches flash/XIP state runs before core 1 starts.
    static pico_toolset::SdCard sd_card;
    static picoboot::AppManager manager(sd_card, picoboot::board::sd_config());
    manager.refresh();
    picoboot::usb_bridge_init(sd_card);
    pico_toolset::LvglDisplayAdapter::s_idle_hook = pump_usb;
    picoboot::usb_bridge_task();

    multicore_launch_core1(core1_main);
    const bool lcd_ok = multicore_fifo_pop_blocking() == 1;
    printf("PicoBoot Presto: LCD %s\n", lcd_ok ? "ok" : "FAILED");
    picoboot::usb_bridge_task();

    static pico_toolset::Ft6236 touch;
    touch.init(pico_toolset::configs::ft6236::kPimoroniPrestoFullRes);

    static pico_toolset::LvglDisplayAdapter adapter;
    const pico_toolset::LvglDisplayConfig draw{g_draw_buffer, sizeof(g_draw_buffer) / sizeof(g_draw_buffer[0])};
    adapter.init_framebuffer_rgb332(g_framebuf, kWidth, kHeight, draw);
    // The FT6236 driver already reports panel pixels: identity calibration.
    adapter.add_touch(touch, {false, 0, kWidth - 1, 0, kHeight - 1});

    static picoboot::LvglUi ui(manager, adapter, /*allow_auto_boot=*/!from_app_request);
    static picoboot::SerialUi serial_ui(manager, /*allow_auto_boot=*/false);
    while (true) {
        ui.poll();
        serial_ui.poll();
        pump_usb();
    }
}
