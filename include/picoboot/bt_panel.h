#pragma once

#include "picoboot/bt_manager.h"

#include "lvgl.h"

namespace picoboot {

// Full-screen Bluetooth panel (LVGL): pairing status and passkey, the list of bonded
// devices (green = connected) with a forget button each, PAIR NEW / STOP and CLOSE.
// Opened from a header button; opens by itself when a keyboard needs its passkey typed.
// While open, the keypad is moved to the panel's own focus group so a keyboard or gamepad
// cannot reach the file list behind it.
class BtPanel {
public:
    explicit BtPanel(BtManager& bt) : m_bt(bt) {}

    // LvglUi::add_header_button() callback.
    static void request_open(void* self) { static_cast<BtPanel*>(self)->m_open_requested = true; }

    // Call from the main loop: opens/closes the panel and refreshes it.
    void poll();

private:
    friend struct BtPanelCallbacks;

    void open();
    void close();
    void refresh(const BtManager::Snapshot& snapshot);
    void rebuild_list(bool force);

    BtManager& m_bt;
    bool m_open_requested = false;
    bool m_close_requested = false;
    bool m_pair_toggle_requested = false;
    int m_forget_requested = -1; // index into m_devices

    lv_obj_t* m_overlay = nullptr;
    lv_obj_t* m_status = nullptr;
    lv_obj_t* m_passkey = nullptr;
    lv_obj_t* m_list = nullptr;
    lv_obj_t* m_pair_label = nullptr;
    lv_group_t* m_group = nullptr;
    lv_group_t* m_previous_group = nullptr;
    lv_indev_t* m_keypad = nullptr;

    pico_toolset::BtHidDevice m_devices[pico_toolset::BluetoothHid::kMaxBonded] = {};
    size_t m_device_count = 0;
    uint32_t m_shown_revision = 0;
    BtManager::Phase m_shown_phase = BtManager::Phase::kIdle;
    uint32_t m_last_list_ms = 0;
};

} // namespace picoboot
