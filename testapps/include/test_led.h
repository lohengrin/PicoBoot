#pragma once

// LED used by the test apps. On the Pico W the LED is wired through the
// CYW43 wireless chip (needs the cyw43 driver); elsewhere it is a plain GPIO
// (PICOBOOT_TEST_LED_PIN, set per board in cmake/picoboot_testapp_board.cmake).
#include "pico/stdlib.h"

#ifdef PICOBOOT_TEST_LED_CYW43
#include "pico/cyw43_arch.h"

inline void test_led_init() { cyw43_arch_init(); }
inline void test_led_set(bool on) { cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on); }
#else
#ifndef PICOBOOT_TEST_LED_PIN
#define PICOBOOT_TEST_LED_PIN 15
#endif

inline void test_led_init() {
    gpio_init(PICOBOOT_TEST_LED_PIN);
    gpio_set_dir(PICOBOOT_TEST_LED_PIN, GPIO_OUT);
}
inline void test_led_set(bool on) { gpio_put(PICOBOOT_TEST_LED_PIN, on); }
#endif
