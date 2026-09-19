// picoboot_lvgl_dvi: bootloader with the LVGL UI on HDMI/DVI. Core 1 runs the
// DVI/TMDS encode loop (same setup as PicoDoom's i_video_dvi.cpp); core 0 runs
// USB, the SD card and the UIs. USB MSC + CDC serial and the serial UI stay
// fully available.
//
//  - Waveshare RP2350-PiZero: 320x240 RGB565 canvas, USB keyboard/mouse/gamepad
//    through the PIO-USB host.
//  - Pico DV carrier + Pico (W) (RP2040, 264 KB RAM): 320x240 RGB332 canvas
//    (half the memory), the carrier's three buttons as a keypad.
// Either way the encoder doubles pixels horizontally and DVI_VERTICAL_REPEAT
// doubles rows, giving square pixels on 640x480p60.

#include "lvgl_ui.h"
#include "serial_ui.h"
#include "board.h"

#include "picoboot/app_manager.h"
#include "picoboot/fastboot.h"
#include "picoboot/flash_layout.h"
#include "picoboot/usb_bridge.h"
#include "picoboot/vtor_jump.h"

#include "pico_toolset/lvgl_display.h"
#include "pico_toolset/sdcard.h"
#include "pico_toolset/sdcard_configs.h"

#if PICO_RP2350
#include "pico_toolset/lvgl_hid.h"
#include "pico_toolset/usb_hid_configs.h"
#include "pico_toolset/usb_hid_host.h"
#else
#include "pico_toolset/lvgl_gpio_keys.h"
#endif

#include "dvi.h"
#include "dvi_serialiser.h"
#include "dvi_timing.h"
#include "common_dvi_pin_configs.h"
extern "C" {
#include "tmds_encode.h"
}

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#if PICO_RP2350
#include "hardware/regs/qmi.h"
#include "hardware/structs/qmi.h"
#endif
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr int kCanvasW = 320;
constexpr int kCanvasH = 240;

#if PICO_RP2350
using Pixel = uint16_t; // RGB565
constexpr size_t kDrawRows = 40;
const dvi_serialiser_cfg& dvi_pins() { return pico_sock_cfg; }
#else
using Pixel = uint8_t; // RGB332 (RRRGGGBB)
constexpr size_t kDrawRows = 20;
const dvi_serialiser_cfg& dvi_pins() { return pimoroni_demo_hdmi_cfg; }
#endif

dvi_inst g_dvi;
alignas(4) Pixel g_framebuf[kCanvasW * kCanvasH];
alignas(64) uint16_t g_draw_buffer[kCanvasW * kDrawRows];
uint32_t g_core1_stack[1024];

void __not_in_flash_func(encode_row)(const Pixel* row) {
    uint32_t* tmdsbuf;
    queue_remove_blocking_u32(&g_dvi.q_tmds_free, &tmdsbuf);
    const uint pixwidth = g_dvi.timing->h_active_pixels;
    const uint words_per_channel = pixwidth / DVI_SYMBOLS_PER_WORD;
    const auto* pix = reinterpret_cast<const uint32_t*>(row);
#if PICO_RP2350
    tmds_encode_data_channel_16bpp(pix, tmdsbuf + 0 * words_per_channel, pixwidth / 2, DVI_16BPP_BLUE_MSB, DVI_16BPP_BLUE_LSB);
    tmds_encode_data_channel_16bpp(pix, tmdsbuf + 1 * words_per_channel, pixwidth / 2, DVI_16BPP_GREEN_MSB, DVI_16BPP_GREEN_LSB);
    tmds_encode_data_channel_16bpp(pix, tmdsbuf + 2 * words_per_channel, pixwidth / 2, DVI_16BPP_RED_MSB, DVI_16BPP_RED_LSB);
#else
    tmds_encode_data_channel_8bpp(pix, tmdsbuf + 0 * words_per_channel, pixwidth / 2, DVI_8BPP_BLUE_MSB, DVI_8BPP_BLUE_LSB);
    tmds_encode_data_channel_8bpp(pix, tmdsbuf + 1 * words_per_channel, pixwidth / 2, DVI_8BPP_GREEN_MSB, DVI_8BPP_GREEN_LSB);
    tmds_encode_data_channel_8bpp(pix, tmdsbuf + 2 * words_per_channel, pixwidth / 2, DVI_8BPP_RED_MSB, DVI_8BPP_RED_LSB);
#endif
    queue_add_blocking_u32(&g_dvi.q_tmds_valid, &tmdsbuf);
}

void __not_in_flash_func(core1_entry)() {
    // Register as a flash_safe_execute() victim so flash writes park this
    // core (running from RAM) instead of racing XIP. Video stalls briefly
    // per 16 KiB block while flashing; the encoder does not use the raw
    // inter-core FIFO, so the lockout handler owning it is safe.
    flash_safe_execute_core_init();
    // The "park this core" request arrives on the SIO FIFO interrupt. The
    // encoder's DMA interrupt fires once per scanline and is lower-numbered
    // (so wins ties at equal priority): on the RP2040 it can starve the
    // request until flash_safe_execute() times out. Make the request outrank it.
    irq_set_priority(SIO_FIFO_IRQ_NUM(get_core_num()), 0);
    dvi_register_irqs_this_core(&g_dvi, DMA_IRQ_0);
    dvi_start(&g_dvi);
    int row = 0;
    for (;;) {
        encode_row(g_framebuf + static_cast<size_t>(row) * kCanvasW);
        row = (row + 1) % kCanvasH;
    }
}

#if PICO_RP2350
// Both USB roles must be serviced frequently: the device stack (MSC/CDC on
// the native port) and the PIO-USB host (keyboard/mouse), polled from core 0
// since core 1 belongs to the DVI encoder.
void pump_usb() {
    picoboot::usb_bridge_task();
    pico_toolset::UsbHidHost::task();
}
#else
void pump_usb() { picoboot::usb_bridge_task(); }

// Pico DV carrier buttons A/B/C (active low, GPIO 7 / 9 / 20):
// A = down, B = up, C = select.
// Diagnostic ("buttons" serial command): reports which free GPIO changes when
// a button is pressed, for carriers whose button pins are not documented.
// Scans only pins the carrier's HDMI/SD/I2S/wireless wiring leaves free, first
// with pull-ups (buttons to ground) and then with pull-downs (buttons to 3V3).
void scan_buttons() {
    static const uint8_t candidates[] = {0, 1, 2, 3, 4, 14, 15, 16, 17, 20, 21};
    constexpr size_t kCount = sizeof(candidates);
    for (int phase = 0; phase < 2; ++phase) {
        const bool pull_up = phase == 0;
        printf("\nButton scan, pins pulled %s: press and release each button (10 s)...\n",
               pull_up ? "UP (button to GND)" : "DOWN (button to 3V3)");
        bool last[kCount];
        for (size_t i = 0; i < kCount; ++i) {
            gpio_init(candidates[i]);
            gpio_set_dir(candidates[i], GPIO_IN);
            if (pull_up) gpio_pull_up(candidates[i]); else gpio_pull_down(candidates[i]);
        }
        sleep_ms(20);
        for (size_t i = 0; i < kCount; ++i) last[i] = gpio_get(candidates[i]);
        for (absolute_time_t end = make_timeout_time_ms(10000); !time_reached(end);) {
            for (size_t i = 0; i < kCount; ++i) {
                const bool now = gpio_get(candidates[i]);
                if (now != last[i]) {
                    printf("  GPIO %u: %d -> %d\n", candidates[i], last[i], now);
                    last[i] = now;
                }
            }
            pump_usb();
            sleep_ms(2);
        }
        for (size_t i = 0; i < kCount; ++i) {
            gpio_disable_pulls(candidates[i]);
            gpio_deinit(candidates[i]);
        }
    }
    printf("Scan finished.\n");
}

constexpr pico_toolset::LvglGpioKey kKeys[] = {
    {7, LV_KEY_NEXT},
    {9, LV_KEY_PREV},
    {20, LV_KEY_ENTER},
};
#endif

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

    // DVI/TMDS bit timing derives directly from clk_sys, which must be exactly
    // 252 MHz for 640x480p60 (anything else stalls the PIO/DMA scanout with
    // no video at all -- same requirement, and same validated recipe, as
    // PicoDoom/TOM6809). Done only *after* the fast-boot check above so a
    // booted application always starts from the pristine power-on clock state.
#if PICO_RP2350
    // Flash SPI = clk_sys / CLKDIV and boot2 programmed it for ~150 MHz, so
    // keep it at 2 (252/2 = 126 MHz); the RP2350 docs require raising the
    // divider *before* raising clk_sys.
    hw_write_masked(&qmi_hw->m[0].timing, 2u << QMI_M0_TIMING_CLKDIV_LSB, QMI_M0_TIMING_CLKDIV_BITS);
#endif
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(252'000, true);

    stdio_init_all();

    static pico_toolset::SdCard sd_card;
    static picoboot::AppManager manager(sd_card, picoboot::board::sd_config());
    manager.refresh();
    picoboot::usb_bridge_init(sd_card);
    pico_toolset::LvglDisplayAdapter::s_idle_hook = pump_usb;

    g_dvi.timing = &dvi_timing_640x480p_60hz;
    g_dvi.ser_cfg = dvi_pins();
#if PICO_RP2350
    pio_set_gpio_base(g_dvi.ser_cfg.pio, 16); // TMDS pins are GPIO32-38 on this board
#endif
    dvi_init(&g_dvi, next_striped_spin_lock_num(), next_striped_spin_lock_num());
    memset(g_framebuf, 0, sizeof(g_framebuf));
    picoboot::usb_bridge_task();
    multicore_reset_core1();
    multicore_launch_core1_with_stack(core1_entry, g_core1_stack, sizeof(g_core1_stack));
    printf("PicoBoot HDMI: DVI 640x480p60, %dx%d canvas\n", kCanvasW, kCanvasH);

#if PICO_RP2350
    // Keyboard / mouse / gamepad on the PIO-USB host port. The mouse cursor
    // range is the canvas itself, so no coordinate scaling is needed.
    static pico_toolset::UsbHidHost hid;
    pico_toolset::UsbHidConfig hid_config = pico_toolset::configs::usb_hid::kWaveshareRp2350PiZeroHdmi;
    hid_config.mouse_max_x = kCanvasW - 1;
    hid_config.mouse_max_y = kCanvasH - 1;
    hid.init(hid_config);
#endif

    static pico_toolset::LvglDisplayAdapter adapter;
    const pico_toolset::LvglDisplayConfig draw{g_draw_buffer, sizeof(g_draw_buffer) / sizeof(g_draw_buffer[0])};
#if PICO_RP2350
    adapter.init_framebuffer(g_framebuf, kCanvasW, kCanvasH, draw);
    pico_toolset::lvgl_hid_init(hid); // before the UI builds its widgets (default focus group)
#else
    adapter.init_framebuffer_rgb332(g_framebuf, kCanvasW, kCanvasH, draw);
    pico_toolset::lvgl_gpio_keys_init({kKeys, sizeof(kKeys) / sizeof(kKeys[0]), /*active_low=*/true});
#endif

    static picoboot::LvglUi ui(manager, adapter, /*allow_auto_boot=*/!from_app_request);
    static picoboot::SerialUi serial_ui(manager, /*allow_auto_boot=*/false);
#if !PICO_RP2350
    serial_ui.add_command("buttons", scan_buttons);
#endif
    while (true) {
        ui.poll();
        serial_ui.poll();
        pump_usb();
    }
}
