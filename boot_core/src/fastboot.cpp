#include "picoboot/fastboot.h"

#include "picoboot/critical_section.h"
#include "pico_toolset/reset_buttons.h"
#include "pico/bootrom.h"

namespace picoboot {

bool FastBoot::consume(BootTag& out_tag) {
    uint32_t raw_tag = 0;
    if (!pico_toolset::consume_pending_watchdog_tag(raw_tag)) {
        return false;
    }
    out_tag = static_cast<BootTag>(raw_tag);
    return true;
}

void FastBoot::reboot_into_app() {
    InterruptGuard guard;
    pico_toolset::watchdog_reboot_with_tag(static_cast<uint32_t>(BootTag::kBootApp));
}

void FastBoot::reboot_into_bootloader() {
    InterruptGuard guard;
    pico_toolset::watchdog_reboot_with_tag(static_cast<uint32_t>(BootTag::kReenterBootloader));
}

void FastBoot::reboot_into_bootsel() {
    InterruptGuard guard;
    reset_usb_boot(0, 0);
}

} // namespace picoboot
