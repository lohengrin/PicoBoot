// Minimal test app for PicoBoot's Phase 3 flash-writer/VTOR-jump testing:
// toggles a GPIO forever, no SD/USB/flash/watchdog code at all. The
// Waveshare RP2350-PiZero has no plain GPIO LED (only a WS2812, which
// needs a PIO driver -- out of scope for a deliberately trivial control
// case), so this drives a fixed GPIO instead; probe it with a scope/meter
// or wire an external LED when running this test.
#include "pico/stdlib.h"

namespace {
#ifndef PICOBOOT_TEST_LED_PIN
#define PICOBOOT_TEST_LED_PIN 15
#endif
constexpr uint kBlinkPin = PICOBOOT_TEST_LED_PIN;
}

int main() {
    gpio_init(kBlinkPin);
    gpio_set_dir(kBlinkPin, GPIO_OUT);

    while (true) {
        gpio_put(kBlinkPin, 1);
        sleep_ms(250);
        gpio_put(kBlinkPin, 0);
        sleep_ms(250);
    }
}
