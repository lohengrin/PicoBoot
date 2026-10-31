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
#include "pico_toolset/fault_handler.h"
#include "hardware/structs/powman.h"

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
pico_toolset::FaultInfo g_fault{};
bool g_fault_seen = false;
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

// The hard fault that caused the last reset, kept for the "fault" command (the serial port is usually
// reopened after the reset, too late for the boot message).
uint32_t g_reset_reason = 0; // POWMAN CHIP_RESET at boot: why the chip last reset

void print_reset_reason() {
    struct Bit { uint32_t mask; const char* name; };
    static const Bit kBits[] = {
        {POWMAN_CHIP_RESET_HAD_POR_BITS, "power-on"},
        {POWMAN_CHIP_RESET_HAD_BOR_BITS, "brown-out"},
        {POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS, "RUN pin low"},
        {POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS, "debug port"},
        {POWMAN_CHIP_RESET_HAD_RESCUE_BITS, "rescue"},
        {POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS, "watchdog (powman)"},
        {POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS, "watchdog (powman async)"},
        {POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS, "watchdog (core)"},
        {POWMAN_CHIP_RESET_HAD_SWCORE_PD_BITS, "core power-down"},
        {POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS, "glitch detector"},
        {POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS, "system reset request"},
        {POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_PSM_BITS, "watchdog (PSM)"},
    };
    printf("Last reset cause (CHIP_RESET 0x%08lX):", static_cast<unsigned long>(g_reset_reason));
    for (const Bit& b : kBits)
        if (g_reset_reason & b.mask) printf(" [%s]", b.name);
    printf("\n");
}

void cmd_fault() {
    print_reset_reason();
    if (!g_fault_seen) {
        printf("No hard fault recorded since power-on\n");
        return;
    }
    printf("Hard fault before this boot: pc 0x%08lX lr 0x%08lX cfsr 0x%08lX\n", static_cast<unsigned long>(g_fault.pc),
           static_cast<unsigned long>(g_fault.lr), static_cast<unsigned long>(g_fault.cfsr));
}

// Serial commands: bt (devices and scan results), btscan (look for devices to pair), btforget (all devices).
void cmd_bt() {
    if (!g_bt || !g_bt->ready()) {
        printf("Bluetooth is not available\n");
        return;
    }
    auto& hid = g_bt->hid();
    uint32_t code;
    printf("Bluetooth: %s", hid.pairing() ? "scanning" : hid.connecting() ? "connecting" : "idle");
    if (hid.pairing_passkey(&code)) printf(", type %06lu on the keyboard", static_cast<unsigned long>(code));
    printf("\n");
    pico_toolset::BtUiDevice devices[pico_toolset::BluetoothDevices::kMaxFound];
    for (int found = 0; found < 2; ++found) {
        const size_t count = found ? g_bt->devices().found(devices, pico_toolset::BluetoothDevices::kMaxFound)
                                   : g_bt->devices().bonded(devices, pico_toolset::BluetoothDevices::kMaxDevices);
        for (size_t i = 0; i < count; ++i) {
            const auto& d = devices[i];
            printf("  %02X:%02X:%02X:%02X:%02X:%02X  %-20s %-8s %s\n", d.addr[0], d.addr[1], d.addr[2], d.addr[3], d.addr[4],
                   d.addr[5], d.name, pico_toolset::bt_ui_kind_name(d.kind),
                   found ? "found" : pico_toolset::bt_ui_state_name(d.state));
        }
        if (count == 0 && !found) printf("  no paired devices\n");
    }
}
void cmd_bt_scan() {
    if (g_bt) g_bt->devices().start_scan();
    printf("Scanning (30 s): put the device in pairing mode, then pair it from the Bluetooth page\n");
}
void cmd_bt_forget() {
    if (g_bt && g_bt->ready()) g_bt->hid().forget_all();
    printf("All Bluetooth devices forgotten\n");
}

} // namespace

// Core 0's linker stack is 4 KiB (all the scratch bank holds) and the 'info' command showed it fully
// used: LVGL's renderer, FatFs and printf run on it, and BTstack's handlers come on top of whatever
// the main loop was doing (interrupt context, same stack). Overflowing it smashed a return address and
// crashed the board (hard fault to address 0). So the main code runs on a larger stack in RAM:
// main() points MSP at it and enters app_main(), which never returns. Interrupts use it too.
constexpr size_t kMainStackBytes = 10 * 1024;
alignas(8) uint8_t g_main_stack[kMainStackBytes];

extern "C" int app_main();

int main() {
    picoboot::stack_use_region(g_main_stack, sizeof(g_main_stack));
    asm volatile("msr msp, %0\n\tbl app_main" ::"r"(g_main_stack + sizeof(g_main_stack)) : "memory");
    __builtin_unreachable();
}

extern "C" int app_main() {
    picoboot::BootTag tag;
    bool from_app_request = false;
    if (picoboot::FastBoot::consume(tag)) {
        picoboot::FastBoot::boot_app_if_tagged(tag);
        from_app_request = (tag == picoboot::BootTag::kReenterBootloader);
    }

    g_reset_reason = powman_hw->chip_reset;
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
    serial_ui.add_command("fault", cmd_fault);
    serial_ui.add_command("bt", cmd_bt);
    serial_ui.add_command("btscan", cmd_bt_scan);
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
            g_fault_seen = pico_toolset::consume_pending_hard_fault(g_fault); // a crash resets the board
            if (g_fault_seen) cmd_fault();
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
            pico_toolset::BtUiDevice bonded[pico_toolset::BluetoothDevices::kMaxDevices];
            printf("Bluetooth %s, %u bonded device(s)\n", bt_ok ? "ready" : "FAILED",
                   static_cast<unsigned>(bt.devices().bonded(bonded, pico_toolset::BluetoothDevices::kMaxDevices)));
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
