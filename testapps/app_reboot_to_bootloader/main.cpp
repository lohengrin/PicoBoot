// Test app for PicoBoot's Phase 3/5 testing: blinks briefly (visible proof
// of life) then requests a return to the bootloader UI, exercising the
// app->bootloader round trip end to end. Links pico_toolset_reset_buttons
// directly and reuses picoboot::BootTag (boot_core/include/picoboot/
// boot_tags.h) so the tag value can never drift out of sync with the
// bootloader's own FastBoot.
#include "picoboot/boot_tags.h"

#include "pico/stdlib.h"
#include "pico_toolset/reset_buttons.h"

namespace {
constexpr uint kBlinkPin = 15; // see app_blink's main.cpp: no plain LED on this board
}

int main() {
    gpio_init(kBlinkPin);
    gpio_set_dir(kBlinkPin, GPIO_OUT);

    for (int i = 0; i < 6; ++i) {
        gpio_put(kBlinkPin, i % 2);
        sleep_ms(150);
    }
    gpio_put(kBlinkPin, 0);

    pico_toolset::watchdog_reboot_with_tag(static_cast<uint32_t>(picoboot::BootTag::kReenterBootloader));
}
