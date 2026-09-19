# Board selection for the standalone test apps (they are separate CMake
# projects, built like any application): -DPICOBOOT_TEST_BOARD=<board>
#   waveshare_pizero (default, RP2350) | crowpanel_pico_hmi_28 (RP2040) | pico_dv (RP2040, Pico W)
# Sets PICO_BOARD / PICO_PLATFORM, PICOBOOT_TEST_FLASH_SIZE and
# PICOBOOT_TEST_LED_PIN (or PICOBOOT_TEST_LED_CYW43 for the Pico W). Include before pico_sdk_import.cmake.
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
    set(PICOBOOT_TEST_LED_PIN 19) # exposed header pin (GPIO15 is not broken out on this board)
elseif(PICOBOOT_TEST_BOARD STREQUAL "pico_dv")
    set(PICO_BOARD pico_w CACHE STRING "" FORCE)
    set(PICO_PLATFORM rp2040 CACHE STRING "" FORCE)
    set(PICOBOOT_TEST_FLASH_SIZE 2097152)
    set(PICOBOOT_TEST_LED_CYW43 ON) # the Pico W's LED is behind the wireless chip
else()
    message(FATAL_ERROR "Unknown PICOBOOT_TEST_BOARD '${PICOBOOT_TEST_BOARD}'")
endif()

# picoboot_testapp_led(TARGET): LED wiring for the test apps (see
# testapps/include/test_led.h).
function(picoboot_testapp_led TARGET)
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../testapps/include)
    if(PICOBOOT_TEST_LED_CYW43)
        target_compile_definitions(${TARGET} PRIVATE PICOBOOT_TEST_LED_CYW43=1)
        target_link_libraries(${TARGET} PRIVATE pico_cyw43_arch_none)
    else()
        target_compile_definitions(${TARGET} PRIVATE PICOBOOT_TEST_LED_PIN=${PICOBOOT_TEST_LED_PIN})
    endif()
endfunction()
