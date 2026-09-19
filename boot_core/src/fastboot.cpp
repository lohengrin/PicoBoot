#include "picoboot/fastboot.h"

#include "picoboot/critical_section.h"
#include "picoboot/flash_layout.h"
#include "picoboot/vtor_jump.h"
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

void FastBoot::reboot_into_app_remapped() {
    InterruptGuard guard;
    pico_toolset::watchdog_reboot_with_tag(static_cast<uint32_t>(BootTag::kBootAppRemapped));
}

void FastBoot::boot_app_if_tagged(BootTag tag) {
    if (tag == BootTag::kBootApp) {
        relocate_vtor_and_jump(kAppFlashBase);
    }
#if PICO_RP2350
    if (tag == BootTag::kBootAppRemapped) {
        jump_to_app_remapped(kAppFlashBase, static_cast<uint32_t>(app_partition_size(PICO_FLASH_SIZE_BYTES)));
    }
#endif
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
