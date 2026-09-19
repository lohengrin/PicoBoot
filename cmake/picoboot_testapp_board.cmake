# Board selection for the standalone test apps (they are separate CMake
# projects, built like any application): -DPICOBOOT_TEST_BOARD=<board>
#   waveshare_pizero (default, RP2350) | crowpanel_pico_hmi_28 (RP2040) | pico_dv (RP2040, Pico W)
# Sets PICO_BOARD / PICO_PLATFORM, PICOBOOT_TEST_FLASH_SIZE and
# PICOBOOT_TEST_LED_PIN. Include before pico_sdk_import.cmake.
set(PICOBOOT_TEST_BOARD "waveshare_pizero" CACHE STRING "Board the test app is built for")
if(PICOBOOT_TEST_BOARD STREQUAL "waveshare_pizero")
    set(PICO_BOARD waveshare_rp2350_pizero CACHE STRING "" FORCE)
    set(PICO_PLATFORM rp2350 CACHE STRING "" FORCE)
    set(PICOBOOT_TEST_FLASH_SIZE 16777216)
    set(PICOBOOT_TEST_LED_PIN 15) # no plain LED on this board: probe/wire an LED
elseif(PICOBOOT_TEST_BOARD STREQUAL "crowpanel_pico_hmi_28")
    set(PICO_BOARD pico CACHE STRING "" FORCE)
    set(PICO_PLATFORM rp2040 CACHE STRING "" FORCE)
    set(PICOBOOT_TEST_FLASH_SIZE 2097152)
    set(PICOBOOT_TEST_LED_PIN 18) # the LCD backlight: visibly blinks
elseif(PICOBOOT_TEST_BOARD STREQUAL "pico_dv")
    set(PICO_BOARD pico_w CACHE STRING "" FORCE)
    set(PICO_PLATFORM rp2040 CACHE STRING "" FORCE)
    set(PICOBOOT_TEST_FLASH_SIZE 2097152)
    set(PICOBOOT_TEST_LED_PIN 15) # probe/wire an LED (the Pico W LED is behind the wireless chip)
else()
    message(FATAL_ERROR "Unknown PICOBOOT_TEST_BOARD '${PICOBOOT_TEST_BOARD}'")
endif()
