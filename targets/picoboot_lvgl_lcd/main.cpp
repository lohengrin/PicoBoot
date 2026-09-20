// picoboot_lvgl_lcd / picoboot_lvgl_lcd_st7796: bootloader with the LVGL UI on
// the external 3.5" 480x320 LCD + XPT2046 touch of the Waveshare RP2350-PiZero.
// The same wiring carries either an ILI9486 panel (Waveshare 3.5" RPi LCD (A),
// the default) or an ST7796U panel (SunFounder 3.5" IPS): the second target is
// this file built with PICOBOOT_LCD_ST7796.

#include "lvgl_ui.h"
#include "serial_ui.h"

#include "picoboot/app_manager.h"
#include "picoboot/fastboot.h"
#include "picoboot/memory_probe.h"
#include "picoboot/flash_layout.h"
#include "picoboot/usb_bridge.h"
#include "picoboot/vtor_jump.h"

#ifdef PICOBOOT_LCD_ST7796
#include "pico_toolset/st7796.h"
#include "pico_toolset/st7796_configs.h"
#else
#include "pico_toolset/ili9486.h"
#include "pico_toolset/ili9486_configs.h"
#endif
#include "pico_toolset/lvgl_display.h"
#include "pico_toolset/sdcard.h"
#include "pico_toolset/sdcard_configs.h"
#include "board.h"
#include "pico_toolset/xpt2046.h"
#include "pico_toolset/xpt2046_calibration.h"
#include "pico_toolset/xpt2046_configs.h"

#include "pico/stdlib.h"

#include <cstdio>

namespace {
constexpr size_t kDrawRows = 40;
alignas(64) uint16_t g_draw_buffer[480 * kDrawRows];
#ifdef PICOBOOT_LCD_ST7796
pico_toolset::St7796 g_lcd;
const auto& lcd_config() { return pico_toolset::configs::st7796::kWaveshareRp2350PiZero; }
#else
pico_toolset::Ili9486 g_lcd;
const auto& lcd_config() { return pico_toolset::configs::ili9486::kWaveshareRp2350PiZero; }
#endif
} // namespace

// Called from LVGL's LV_ASSERT_HANDLER (see ui/lvgl/lv_conf.h): show a red
// screen and repeat the message over CDC instead of hanging silently.
extern "C" void picoboot_lvgl_assert(void) {
    g_lcd.fill_solid(0xF800);
    while (true) {
        printf("LVGL assertion failed\n");
        for (absolute_time_t end = make_timeout_time_ms(1000); !time_reached(end);) {
            picoboot::usb_bridge_task();
        }
    }
}

int main() {
    picoboot::BootTag tag;
    bool from_app_request = false;
    if (picoboot::FastBoot::consume(tag)) {
        picoboot::FastBoot::boot_app_if_tagged(tag);
        from_app_request = (tag == picoboot::BootTag::kReenterBootloader);
    }

    picoboot::stack_paint(); // for the 'info' command's stack headroom
    stdio_init_all();

    static pico_toolset::SdCard sd_card;
    static picoboot::AppManager manager(sd_card, picoboot::board::sd_config());
    manager.refresh();
    picoboot::usb_bridge_init(sd_card);
    // Keep the USB device serviced even while a long redraw/flash is going on.
    pico_toolset::LvglDisplayAdapter::s_idle_hook = picoboot::usb_bridge_task;

    picoboot::usb_bridge_task();
    g_lcd.init(lcd_config());
    picoboot::usb_bridge_task();
    g_lcd.fill_solid(0x0000); // clear the panel's power-up noise before the first frame
    printf("PicoBoot LVGL: panel ok\n");
    static pico_toolset::Xpt2046Touch touch;
    touch.init(pico_toolset::configs::xpt2046::kWaveshareRp2350PiZero);

    static pico_toolset::LvglDisplayAdapter adapter;
    adapter.init(g_lcd, {g_draw_buffer, sizeof(g_draw_buffer) / sizeof(g_draw_buffer[0])});
    const pico_toolset::Xpt2046Calibration cal;
    adapter.add_touch(touch, {cal.swap_axes, cal.raw_h_min, cal.raw_h_max, cal.raw_v_min, cal.raw_v_max});
    printf("PicoBoot LVGL: adapter ok\n");

    // Both UIs run side by side: the LCD is the primary one (owns the
    // auto-boot countdown); the serial menu stays fully usable over CDC.
    static picoboot::LvglUi ui(manager, adapter, /*allow_auto_boot=*/!from_app_request);
    static picoboot::SerialUi serial_ui(manager, /*allow_auto_boot=*/false);
    while (true) {
        ui.poll();
        serial_ui.poll();
        picoboot::usb_bridge_task();
    }
}
