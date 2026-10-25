#include "picoboot/app_manager.h"
#include "picoboot/board.h"
#include "picoboot/bt_manager.h"
#include "picoboot/bt_panel.h"
#include "picoboot/fastboot.h"
#include "picoboot/lvgl_input.h"
#include "picoboot/lvgl_ui.h"
#include "picoboot/memory_probe.h"
#include "picoboot/serial_ui.h"
#include "picoboot/usb_bridge.h"

#include "pico_toolset/ft6236.h"
#include "pico_toolset/ft6236_configs.h"
#include "pico_toolset/lvgl_display.h"
#include "pico_toolset/qwstpad_configs.h"
#include "pico_toolset/st7701.h"
#include "pico_toolset/st7701_configs.h"

#include "hardware/i2c.h"
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
picoboot::BtManager* g_bt = nullptr; // for the serial commands (plain function pointers)

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

// QW/ST connector: I2C0 on GPIO40 (SDA) / GPIO41 (SCL).
constexpr uint kQwstSdaPin = 40, kQwstSclPin = 41;

// Serial commands: bt (status and devices), btpair, btforget (all devices).
void cmd_bt() {
    if (!g_bt || !g_bt->ready()) {
        printf("Bluetooth is not available\n");
        return;
    }
    static const char* const kPhase[] = {"idle", "scanning", "connecting", "passkey", "paired", "failed"};
    const auto snap = g_bt->snapshot();
    printf("Bluetooth: %s", kPhase[static_cast<int>(snap.phase)]);
    if (snap.phase == picoboot::BtManager::Phase::kPasskey) printf(", type %06lu on the keyboard", static_cast<unsigned long>(snap.passkey));
    printf("\n");
    pico_toolset::BtHidDevice devices[pico_toolset::BluetoothHid::kMaxBonded];
    const size_t count = g_bt->bonded(devices, pico_toolset::BluetoothHid::kMaxBonded);
    for (size_t i = 0; i < count; ++i) {
        const auto& d = devices[i];
        printf("  %02X:%02X:%02X:%02X:%02X:%02X  %-20s %s%s%s%s\n", d.addr[0], d.addr[1], d.addr[2], d.addr[3], d.addr[4],
               d.addr[5], d.name, d.connected ? "connected" : "bonded",
               (d.kinds & pico_toolset::bt_hid_kind::Keyboard) ? " keyboard" : "",
               (d.kinds & pico_toolset::bt_hid_kind::Mouse) ? " mouse" : "",
               (d.kinds & pico_toolset::bt_hid_kind::Gamepad) ? " gamepad" : "");
    }
    if (count == 0) printf("  no paired devices\n");
}
void cmd_bt_pair() {
    if (g_bt) g_bt->start_pairing();
    printf("Pairing mode on: put the device in pairing mode (30 s)\n");
}
void cmd_bt_forget() {
    if (g_bt) g_bt->forget_all();
    printf("All Bluetooth devices forgotten\n");
}

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

    // QwSTPad on the QW/ST connector (optional: absent is fine).
    i2c_init(i2c0, 400'000);
    gpio_set_function(kQwstSdaPin, GPIO_FUNC_I2C);
    gpio_set_function(kQwstSclPin, GPIO_FUNC_I2C);
    gpio_pull_up(kQwstSdaPin);
    gpio_pull_up(kQwstSclPin);
    static pico_toolset::QwstPad pad;
    const bool pad_ok = pad.init(pico_toolset::configs::qwstpad::kPimoroniPresto);
    printf("QwSTPad %s\n", pad_ok ? "found" : "not found");

    // Input devices first (they create the default focus group the widgets join), then the UI.
    static picoboot::BtManager bt;
    g_bt = &bt;
    picoboot::lvgl_input_init(bt, pad_ok ? &pad : nullptr, kWidth, kHeight);

    static picoboot::LvglUi ui(manager, adapter, /*allow_auto_boot=*/!from_app_request);
    static picoboot::BtPanel bt_panel(bt);
    ui.add_header_button(LV_SYMBOL_BLUETOOTH, picoboot::BtPanel::request_open, &bt_panel);
    static picoboot::SerialUi serial_ui(manager, /*allow_auto_boot=*/false);
    serial_ui.add_command("bt", cmd_bt);
    serial_ui.add_command("btpair", cmd_bt_pair);
    serial_ui.add_command("btforget", cmd_bt_forget);

    ui.poll(); // builds the screen and renders the first frame before the radio comes up
    bt.init(); // ~1 s: loads the CYW43 firmware and the bonded devices
    while (true) {
        if (bt.ready() && bt.hid().consume_key_press(0x46)) ui.request_screenshot(); // PrintScreen
        ui.poll();
        bt_panel.poll();
        serial_ui.poll();
        pump_usb();
    }
}
