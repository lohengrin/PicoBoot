// picoboot_lvgl_dvi: bootloader with the LVGL UI on the onboard HDMI/DVI
// output of the Waveshare RP2350-PiZero. Core 1 runs the DVI/TMDS encode
// loop (see PicoDoom's i_video_dvi.cpp for the same setup); core 0 runs
// USB, the SD card and the UIs. USB MSC + CDC serial and the serial UI stay
// fully available.

#include "lvgl_ui.h"
#include "serial_ui.h"

#include "picoboot/app_manager.h"
#include "picoboot/fastboot.h"
#include "picoboot/flash_layout.h"
#include "picoboot/usb_bridge.h"
#include "picoboot/vtor_jump.h"

#include "pico_toolset/lvgl_display.h"
#include "pico_toolset/sdcard.h"
#include "pico_toolset/sdcard_configs.h"

#include "dvi.h"
#include "dvi_serialiser.h"
#include "dvi_timing.h"
#include "common_dvi_pin_configs.h"
extern "C" {
#include "tmds_encode.h"
}

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/regs/qmi.h"
#include "hardware/structs/qmi.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include <cstdio>
#include <cstring>

namespace {

// 320x240 canvas: the encoder doubles each pixel horizontally and
// DVI_VERTICAL_REPEAT doubles rows, giving square pixels on 640x480.
constexpr int kCanvasW = 320;
constexpr int kCanvasH = 240;
constexpr size_t kDrawRows = 40;

dvi_inst g_dvi;
alignas(4) uint16_t g_framebuf[kCanvasW * kCanvasH];
alignas(64) uint16_t g_draw_buffer[kCanvasW * kDrawRows];
uint32_t g_core1_stack[1024];

void __not_in_flash_func(encode_row)(const uint16_t* row) {
    uint32_t* tmdsbuf;
    queue_remove_blocking_u32(&g_dvi.q_tmds_free, &tmdsbuf);
    const uint pixwidth = g_dvi.timing->h_active_pixels;
    const uint words_per_channel = pixwidth / DVI_SYMBOLS_PER_WORD;
    const auto* pix = reinterpret_cast<const uint32_t*>(row);
    tmds_encode_data_channel_16bpp(pix, tmdsbuf + 0 * words_per_channel, pixwidth / 2,
                                   DVI_16BPP_BLUE_MSB, DVI_16BPP_BLUE_LSB);
    tmds_encode_data_channel_16bpp(pix, tmdsbuf + 1 * words_per_channel, pixwidth / 2,
                                   DVI_16BPP_GREEN_MSB, DVI_16BPP_GREEN_LSB);
    tmds_encode_data_channel_16bpp(pix, tmdsbuf + 2 * words_per_channel, pixwidth / 2,
                                   DVI_16BPP_RED_MSB, DVI_16BPP_RED_LSB);
    queue_add_blocking_u32(&g_dvi.q_tmds_valid, &tmdsbuf);
}

void __not_in_flash_func(core1_entry)() {
    // Register as a flash_safe_execute() victim so flash writes park this
    // core (running from RAM) instead of racing XIP. Video stalls briefly
    // per 16 KiB block while flashing; the encoder does not use the raw
    // inter-core FIFO, so the lockout handler owning it is safe.
    flash_safe_execute_core_init();
    dvi_register_irqs_this_core(&g_dvi, DMA_IRQ_0);
    dvi_start(&g_dvi);
    int row = 0;
    for (;;) {
        encode_row(g_framebuf + static_cast<size_t>(row) * kCanvasW);
        row = (row + 1) % kCanvasH;
    }
}

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
    // PicoDoom/TOM6809 on this board). Done only *after* the fast-boot check
    // above so a booted application always starts from the pristine
    // power-on clock state.
    //
    // 1. Flash QMI clock divider first: flash SPI = clk_sys / CLKDIV and boot2
    //    programmed it for ~150 MHz, so keep it at 2 (252/2 = 126 MHz); the
    //    RP2350 docs require increasing the divider *before* raising clk_sys.
    hw_write_masked(&qmi_hw->m[0].timing, 2u << QMI_M0_TIMING_CLKDIV_LSB, QMI_M0_TIMING_CLKDIV_BITS);
    // 2. Core voltage for 252 MHz, then the system clock.
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(252'000, true);

    stdio_init_all();

    static pico_toolset::SdCard sd_card;
    static picoboot::AppManager manager(sd_card, pico_toolset::configs::sdcard::kWaveshareRp2350PiZero);
    manager.refresh();
    picoboot::usb_bridge_init(sd_card);
    pico_toolset::LvglDisplayAdapter::s_idle_hook = picoboot::usb_bridge_task;

    g_dvi.timing = &dvi_timing_640x480p_60hz;
    g_dvi.ser_cfg = pico_sock_cfg;
    pio_set_gpio_base(g_dvi.ser_cfg.pio, 16); // TMDS pins are GPIO32-38 on this board
    dvi_init(&g_dvi, next_striped_spin_lock_num(), next_striped_spin_lock_num());
    memset(g_framebuf, 0, sizeof(g_framebuf));
    picoboot::usb_bridge_task();
    multicore_reset_core1();
    multicore_launch_core1_with_stack(core1_entry, g_core1_stack, sizeof(g_core1_stack));
    printf("PicoBoot HDMI: DVI 640x480p60, %dx%d canvas\n", kCanvasW, kCanvasH);

    static pico_toolset::LvglDisplayAdapter adapter;
    adapter.init_framebuffer(g_framebuf, kCanvasW, kCanvasH,
                             {g_draw_buffer, sizeof(g_draw_buffer) / sizeof(g_draw_buffer[0])});

    static picoboot::LvglUi ui(manager, adapter, /*allow_auto_boot=*/!from_app_request);
    static picoboot::SerialUi serial_ui(manager, /*allow_auto_boot=*/false);
    while (true) {
        ui.poll();
        serial_ui.poll();
        picoboot::usb_bridge_task();
    }
}
