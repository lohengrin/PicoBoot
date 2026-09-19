// picoboot_serial: bootloader with the serial (CDC) UI.

#include "serial_ui.h"

#include "picoboot/app_manager.h"
#include "picoboot/fastboot.h"
#include "picoboot/flash_layout.h"
#include "picoboot/usb_bridge.h"
#include "picoboot/vtor_jump.h"

#include "pico_toolset/sdcard.h"
#include "pico_toolset/sdcard_configs.h"
#include "board.h"

#include "pico/stdlib.h"

int main() {
    // First thing, before any peripheral init: kBootApp is the only tag
    // that skips init and jumps straight into the flashed application.
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

    // Let the host enumerate and open the CDC port before the first menu.
    for (absolute_time_t end = make_timeout_time_ms(2000); !time_reached(end);) {
        picoboot::usb_bridge_task();
    }

    static picoboot::SerialUi ui(manager, /*allow_auto_boot=*/!from_app_request);
    while (true) {
        ui.poll();
        picoboot::usb_bridge_task();
    }
}
