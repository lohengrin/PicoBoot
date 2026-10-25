#include "picoboot/app_manager.h"
#include "picoboot/board.h"
#include "picoboot/bt_manager.h"
#include "picoboot/bt_panel.h"
#include "picoboot/fastboot.h"
#include "picoboot/lvgl_input.h"
#include "picoboot/lvgl_ui.h"
#include "picoboot/memory_probe.h"
#include "picoboot/serial_ui.h"
#include "picoboot/trace.h"
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
constexpr uint32_t kBluetoothStartMs = 3000; // see the main loop

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

    // USB first: the drive needs only the card, so mount it, start USB and let the host enumerate
    // while the catalog scan, the LCD and (3 s in) the radio come up behind it.
    static pico_toolset::SdCard sd_card;
    static picoboot::AppManager manager(sd_card, picoboot::board::sd_config());
    manager.mount();
    picoboot::trace::mark("sd_mounted");
    picoboot::usb_bridge_init(sd_card);
    pico_toolset::LvglDisplayAdapter::s_idle_hook = pump_usb;
    pump_usb();
    manager.scan();
    picoboot::trace::mark("catalog_scanned");
    pump_usb();

    // Core 1 initialises the panel (~0.4 s) and then only serves the scanout interrupts: core 0
    // does not wait for it. Flash writes by core 0 (Bluetooth bonding, loading an app) must not
    // happen before it has finished, since its init code still runs from flash.
    multicore_launch_core1(core1_main);
    bool lcd_pending = true;
    bool lcd_ok = false;

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
#ifdef PICOBOOT_USB_STATS
    serial_ui.add_command("stats", [] { picoboot::trace::print(); });
#endif

    ui.poll(); // builds the screen
    picoboot::trace::mark("ui_built");

    bool bt_started = false;
    while (true) {
        if (lcd_pending && multicore_fifo_rvalid()) {
            lcd_pending = false;
            lcd_ok = multicore_fifo_pop_blocking() == 1;
            printf("PicoBoot Presto: LCD %s\n", lcd_ok ? "ok" : "FAILED");
            picoboot::trace::mark(lcd_ok ? "lcd_ready" : "lcd_FAILED");
        }
        // The radio costs ~1 s with the USB stack unserviced (it loads the CYW43 firmware), so it
        // starts a fixed time after boot, once the host has had time to enumerate and read the
        // drive's first sectors -- and only after the panel init no longer needs flash.
        if (!bt_started && !lcd_pending && to_ms_since_boot(get_absolute_time()) >= kBluetoothStartMs) {
            bt_started = true;
            picoboot::trace::mark("bt_init_start");
            // ~1 s of radio bring-up: the USB stack is serviced from a timer interrupt meanwhile.
            // Nothing may touch the card or print (TinyUSB) from this thread until it is stopped.
            picoboot::usb_bridge_irq_pump_start();
            bool bt_ok;
            {
                picoboot::trace::Scope timed("bt_init");
                bt_ok = bt.init();
            }
            picoboot::usb_bridge_irq_pump_stop();
            picoboot::trace::mark("bt_init_done");
            pico_toolset::BtHidDevice bonded[pico_toolset::BluetoothHid::kMaxBonded];
            printf("Bluetooth %s, %u bonded device(s)\n", bt_ok ? "ready" : "FAILED",
                   static_cast<unsigned>(bt.bonded(bonded, pico_toolset::BluetoothHid::kMaxBonded)));
        }
        if (bt.ready() && bt.hid().consume_key_press(0x46)) ui.request_screenshot(); // PrintScreen
        ui.poll();
        {
            picoboot::trace::Scope timed("bt_panel");
            bt_panel.poll();
        }
        {
            picoboot::trace::Scope timed("serial_ui");
            serial_ui.poll();
        }
        pump_usb();
    }
}
