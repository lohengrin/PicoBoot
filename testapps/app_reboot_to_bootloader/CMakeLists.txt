# Test app: blinks briefly then requests a return to the bootloader via
# the same watchdog-tag mechanism PicoBoot's own FastBoot uses. Exercises
# the app-to-bootloader round trip end to end (Phase 3/5). Links
# pico_toolset_reset_buttons directly (the same component the bootloader
# itself uses) and shares boot_core's boot_tags.h so the tag values can
# never drift out of sync between the two.
cmake_minimum_required(VERSION 3.20)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_testapp_board.cmake)
set(TOOLSET_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../third_party/pico-toolset)
list(APPEND PICO_BOARD_HEADER_DIRS ${TOOLSET_ROOT}/boards)

if(NOT PICO_SDK_PATH)
    if(DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH} CACHE PATH "" FORCE)
    else()
        message(FATAL_ERROR "PICO_SDK_PATH must be set (env or -D) to build this test app.")
    endif()
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(app_reboot_to_bootloader C CXX ASM)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
pico_sdk_init()

add_subdirectory(${TOOLSET_ROOT}/components/reset_buttons ${CMAKE_BINARY_DIR}/_toolset/reset_buttons)

add_executable(app_reboot_to_bootloader main.cpp)
target_link_libraries(app_reboot_to_bootloader PRIVATE
    pico_stdlib
    pico_toolset_reset_buttons
)
target_include_directories(app_reboot_to_bootloader PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../boot_core/include
)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/picoboot_app_linker.cmake)
# The board's real total flash size -- app partition = this minus the boot reserve.
picoboot_set_app_flash_region(app_reboot_to_bootloader ${PICOBOOT_TEST_FLASH_SIZE})

target_compile_definitions(app_reboot_to_bootloader PRIVATE PICOBOOT_TEST_LED_PIN=${PICOBOOT_TEST_LED_PIN})
pico_add_extra_outputs(app_reboot_to_bootloader)
