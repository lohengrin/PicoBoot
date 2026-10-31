#pragma once

#include "picoboot/bt_manager.h"

#include "pico_toolset/bluetooth_page_controller.h"
#include "pico_toolset/bluetooth_page_lvgl.h"

#include "lvgl.h"

namespace picoboot {

// Full-screen Bluetooth page (the toolset's common page, LVGL view) as an overlay: bonded devices
// with their state, pairing with a list of what was found, connect / forget with confirmation.
// Opened from a header button; opens by itself when a keyboard needs its passkey typed.
// While open, the keypad is moved to the page's own focus group so a keyboard or gamepad
// cannot reach the file list behind it.
class BtPanel {
public:
    explicit BtPanel(BtManager& bt);

    // LvglUi::add_header_button() callback.
    static void request_open(void* self) { static_cast<BtPanel*>(self)->m_open_requested = true; }

    // Call from the main loop: opens/closes the page and refreshes it.
    void poll();

private:
    void open();
    void close();

    BtManager& m_bt;
    pico_toolset::BluetoothPageController m_controller;
    pico_toolset::BluetoothPageLvgl m_page;
    bool m_open_requested = false;

    lv_obj_t* m_overlay = nullptr;
    lv_group_t* m_previous_group = nullptr;
    lv_indev_t* m_keypad = nullptr;
    uint32_t m_last_update_ms = 0;
};

} // namespace picoboot
