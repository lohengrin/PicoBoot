// picoboot_lvgl_lcd: bootloader with the LVGL UI on the external 3.5"
// ILI9486 LCD + XPT2046 touch.

#include "lvgl_ui.h"

#include "picoboot/app_manager.h"
#include "picoboot/fastboot.h"
#include "picoboot/flash_layout.h"
#include "picoboot/usb_bridge.h"
#include "picoboot/vtor_jump.h"

#include "pico_toolset/ili9486.h"
#include "pico_toolset/ili9486_configs.h"
#include "pico_toolset/lvgl_display.h"
#include "pico_toolset/sdcard.h"
#include "pico_toolset/sdcard_configs.h"
#include "pico_toolset/xpt2046.h"
#include "pico_toolset/xpt2046_calibration.h"
#include "pico_toolset/xpt2046_configs.h"

#include "pico/stdlib.h"

namespace {
constexpr size_t kDrawRows = 40;
uint16_t g_draw_buffer[480 * kDrawRows];
} // namespace

int main() {
    picoboot::BootTag tag;
    bool from_app_request = false;
    if (picoboot::FastBoot::consume(tag)) {
        if (tag == picoboot::BootTag::kBootApp) {
            picoboot::relocate_vtor_and_jump(picoboot::kAppFlashBase);
        }
        from_app_request = (tag == picoboot::BootTag::kReenterBootloader);
    }

    stdio_init_all();

    static pico_toolset::Ili9486 lcd;
    lcd.init(pico_toolset::configs::ili9486::kWaveshareRp2350PiZero);
    static pico_toolset::Xpt2046Touch touch;
    touch.init(pico_toolset::configs::xpt2046::kWaveshareRp2350PiZero);

    static pico_toolset::LvglDisplayAdapter adapter;
    adapter.init(lcd, {g_draw_buffer, sizeof(g_draw_buffer) / sizeof(g_draw_buffer[0])});
    const pico_toolset::Xpt2046Calibration cal;
    adapter.add_touch(touch, {cal.swap_axes, cal.raw_h_min, cal.raw_h_max, cal.raw_v_min, cal.raw_v_max});

    static pico_toolset::SdCard sd_card;
    static picoboot::AppManager manager(sd_card, pico_toolset::configs::sdcard::kWaveshareRp2350PiZero);
    manager.refresh();
    picoboot::usb_bridge_init(sd_card);

    static picoboot::LvglUi ui(manager, adapter, /*allow_auto_boot=*/!from_app_request);
    while (true) {
        ui.poll();
        picoboot::usb_bridge_task();
    }
}
