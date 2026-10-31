#pragma once

#include "pico_toolset/bluetooth.h"
#include "pico_toolset/bluetooth_devices.h"
#include "pico_toolset/bluetooth_hid.h"
#include "pico_toolset/bluetooth_ui_hid.h"

#include <cstddef>
#include <cstdint>

namespace picoboot {

// Bluetooth LE HID host (keyboard / mouse / gamepad) for boards with the CYW43 radio (Presto).
// Owns the radio bring-up; pairing and the device list are the toolset's common Bluetooth page
// (BluetoothDevices + BluetoothPageController, shown by BtPanel), the input devices (LvglInput)
// read the HID state.
class BtManager {
public:
    // Brings the radio up (blocks ~1 s while the CYW43 firmware loads) and loads the bonded
    // devices. False if the radio or the HID host failed; the manager then stays inert (its
    // device list has no backend, so the page is empty).
    bool init();
    [[nodiscard]] bool ready() const { return m_ready; }

    // Input state, valid once ready().
    [[nodiscard]] pico_toolset::BluetoothHid& hid() { return m_hid; }
    // What the Bluetooth page lists and drives.
    [[nodiscard]] pico_toolset::BluetoothDevices& devices() { return m_devices; }

private:
    pico_toolset::Bluetooth m_bt;
    pico_toolset::BluetoothHid m_hid;
    pico_toolset::BtUiHidBackend m_backend{m_hid};
    pico_toolset::BluetoothDevices m_devices;
    bool m_ready = false;
};

} // namespace picoboot
