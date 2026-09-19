// picoboot_lvgl_crowpanel: bootloader with the LVGL UI on the CrowPanel
// PICO HMI 2.8" (RP2040, 320x240 ST7789 + XPT2046 touch + uSD on SPI1).

#include "lvgl_ui.h"
#include "serial_ui.h"
#include "board.h"

#include "picoboot/app_manager.h"
#include "picoboot/fastboot.h"
#include "picoboot/flash_layout.h"
#include "picoboot/usb_bridge.h"
#include "picoboot/vtor_jump.h"

#include "pico_toolset/lvgl_display.h"
#include "pico_toolset/st7789.h"
#include "pico_toolset/st7789_configs.h"
#include "pico_toolset/xpt2046.h"
#include "pico_toolset/xpt2046_configs.h"

#include "pico/stdlib.h"

#include <cstdio>

namespace {
constexpr size_t kDrawRows = 30;
alignas(64) uint16_t g_draw_buffer[320 * kDrawRows];
pico_toolset::St7789 g_lcd;
} // namespace

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
        if (tag == picoboot::BootTag::kBootApp) {
            picoboot::relocate_vtor_and_jump(picoboot::kAppFlashBase);
        }
        from_app_request = (tag == picoboot::BootTag::kReenterBootloader);
    }

    stdio_init_all();

    static pico_toolset::SdCard sd_card;
    static picoboot::AppManager manager(sd_card, picoboot::board::sd_config());
    manager.refresh();
    picoboot::usb_bridge_init(sd_card);
    pico_toolset::LvglDisplayAdapter::s_idle_hook = picoboot::usb_bridge_task;

    picoboot::usb_bridge_task();
    g_lcd.init(pico_toolset::configs::st7789::kElecrowCrowPanelPicoHmi28);
    g_lcd.set_backlight(255);
    picoboot::usb_bridge_task();
    g_lcd.fill_solid(0x0000); // clear power-up noise before the first frame

    // Touch shares SPI1 with the panel and the SD card (separate CS lines).
    static pico_toolset::Xpt2046Touch touch;
    auto touch_config = pico_toolset::configs::xpt2046::kElecrowCrowPanelPicoHmi28;
    touch_config.spi_instance = g_lcd.spi();
    touch.init(touch_config);

    static pico_toolset::LvglDisplayAdapter adapter;
    adapter.init(g_lcd, {g_draw_buffer, sizeof(g_draw_buffer) / sizeof(g_draw_buffer[0])});

    // Calibration measured on the board: raw (x,y) is ~(279,273) at the
    // top-left corner and ~(3785,3716) at the bottom-right, both rising
    // toward the bottom-right (no inversion). Axis swap is assumed off --
    // confirm with a top-right touch (expect raw x high, raw y low). Define
    // PICO_TOOLSET_LVGL_TOUCH_DEBUG (see the target's CMakeLists.txt) to print
    // raw and mapped coordinates over serial.
    pico_toolset::LvglTouchCalibration cal;
    cal.swap_axes = false;
    cal.raw_h_min = 279; cal.raw_h_max = 3785;
    cal.raw_v_min = 273; cal.raw_v_max = 3716;
    adapter.add_touch(touch, cal);

    static picoboot::LvglUi ui(manager, adapter, /*allow_auto_boot=*/!from_app_request);
    static picoboot::SerialUi serial_ui(manager, /*allow_auto_boot=*/false);
    while (true) {
        ui.poll();
        serial_ui.poll();
        picoboot::usb_bridge_task();
    }
}
