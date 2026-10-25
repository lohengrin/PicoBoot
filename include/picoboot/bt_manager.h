#pragma once

#include "pico_toolset/bluetooth.h"
#include "pico_toolset/bluetooth_hid.h"

#include <cstddef>
#include <cstdint>

namespace picoboot {

// Bluetooth LE HID host (keyboard / mouse / gamepad) for boards with the CYW43 radio (Presto).
// Owns the radio bring-up and the pairing state machine; the UI (BtPanel) and the input
// devices (LvglInput) only read it. The toolset's events arrive in the BTstack context, so
// they are folded into a small snapshot that any thread can read consistently.
class BtManager {
public:
    enum class Phase : uint8_t {
        kIdle,
        kScanning,   // pairing mode: looking for a device that is itself in pairing mode
        kConnecting,
        kPasskey,    // a keyboard asks for the code shown here to be typed on it
        kPaired,
        kFailed,
    };
    enum class Result : uint8_t { kNone, kConnected, kTimedOut };

    struct Snapshot {
        bool ready = false;     // radio and HID host are up
        Phase phase = Phase::kIdle;
        Result result = Result::kNone; // how the last pairing attempt ended (kIdle only)
        uint32_t passkey = 0;
        uint8_t fail_status = 0;
        uint8_t fail_reason = 0;
        char device_name[24] = {};
        uint32_t revision = 0;  // bumped on every event: the device list may have changed
    };

    // Brings the radio up (blocks ~1 s while the CYW43 firmware loads) and loads the bonded
    // devices. False if the radio or the HID host failed; the manager then stays inert.
    bool init();
    [[nodiscard]] bool ready() const { return m_ready; }
    [[nodiscard]] Snapshot snapshot() const;

    void start_pairing();
    void stop_pairing();
    [[nodiscard]] size_t bonded(pico_toolset::BtHidDevice* out, size_t max) const;
    bool forget(const uint8_t addr[6]);
    void forget_all();

    // Input state, valid once ready().
    [[nodiscard]] pico_toolset::BluetoothHid& hid() { return m_hid; }

private:
    static void on_event_thunk(const pico_toolset::BtHidEvent& event, void* user);
    void on_event(const pico_toolset::BtHidEvent& event);

    pico_toolset::Bluetooth m_bt;
    pico_toolset::BluetoothHid m_hid;
    bool m_ready = false;

    // Single writer (the BTstack context), many readers: a sequence lock.
    volatile uint32_t m_seq = 0;
    Snapshot m_state;
};

} // namespace picoboot
