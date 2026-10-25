#include "picoboot/bt_manager.h"

#include "hardware/sync.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include <cstdio>
#include <cstring>

namespace picoboot {

bool BtManager::init() {
    if (cyw43_arch_init() != 0) {
        printf("Bluetooth: radio init failed\n");
        return false;
    }
    if (!m_bt.init(pico_toolset::BluetoothConfig{.name = "PicoBoot"})) {
        printf("Bluetooth: controller did not start\n");
        return false;
    }
    pico_toolset::BluetoothHidConfig config;
    config.callback = on_event_thunk;
    config.user = this;
    config.enable_classic = false; // BLE only: keyboards, mice and gamepads that speak HID over GATT
    if (!m_hid.init(config)) {
        printf("Bluetooth: HID host failed\n");
        return false;
    }
    m_state.ready = true;
    m_ready = true;
    pico_toolset::BtHidDevice devices[pico_toolset::BluetoothHid::kMaxBonded];
    printf("Bluetooth ready: %u bonded device(s)\n", static_cast<unsigned>(bonded(devices, pico_toolset::BluetoothHid::kMaxBonded)));
    return true;
}

BtManager::Snapshot BtManager::snapshot() const {
    Snapshot copy;
    for (;;) {
        const uint32_t before = m_seq;
        if (before & 1) continue; // a write is in progress
        __dmb();
        copy = m_state;
        __dmb();
        if (before == m_seq) return copy;
    }
}

void BtManager::on_event_thunk(const pico_toolset::BtHidEvent& event, void* user) {
    static_cast<BtManager*>(user)->on_event(event);
}

void BtManager::on_event(const pico_toolset::BtHidEvent& event) {
    using pico_toolset::BtHidEventType;
    m_seq = m_seq + 1; // odd: readers retry
    __dmb();
    Snapshot& s = m_state;
    auto remember_name = [&] {
        if (event.device.name[0]) {
            strncpy(s.device_name, event.device.name, sizeof(s.device_name) - 1);
            s.device_name[sizeof(s.device_name) - 1] = 0;
        }
    };
    switch (event.type) {
        case BtHidEventType::PairingStarted:
            s.phase = Phase::kScanning;
            s.result = Result::kNone;
            s.device_name[0] = 0;
            break;
        case BtHidEventType::DeviceFound:
            remember_name();
            break;
        case BtHidEventType::Connecting:
            s.phase = Phase::kConnecting;
            remember_name();
            break;
        case BtHidEventType::PasskeyDisplay:
            s.phase = Phase::kPasskey;
            s.passkey = event.passkey;
            remember_name();
            break;
        case BtHidEventType::Paired:
            s.phase = Phase::kPaired;
            remember_name();
            break;
        case BtHidEventType::Connected:
            // Also fires for a bonded device reconnecting: only a pairing in progress ends here.
            if (s.phase != Phase::kIdle && s.phase != Phase::kFailed) {
                s.phase = Phase::kIdle;
                s.result = Result::kConnected;
            }
            remember_name();
            break;
        case BtHidEventType::PairingStopped:
            if (s.phase == Phase::kScanning) {
                s.phase = Phase::kIdle;
                s.result = Result::kTimedOut;
            }
            break;
        case BtHidEventType::PairingFailed:
            s.phase = Phase::kFailed;
            s.fail_status = event.status;
            s.fail_reason = event.reason;
            break;
        case BtHidEventType::Disconnected:
        case BtHidEventType::Forgotten:
            break;
    }
    ++s.revision;
    __dmb();
    m_seq = m_seq + 1; // even: consistent
}

void BtManager::start_pairing() {
    if (m_ready) m_hid.start_pairing();
}

void BtManager::stop_pairing() {
    if (m_ready) m_hid.stop_pairing();
}

size_t BtManager::bonded(pico_toolset::BtHidDevice* out, size_t max) const {
    return m_ready ? m_hid.bonded_devices(out, max) : 0;
}

bool BtManager::forget(const uint8_t addr[6]) {
    return m_ready && m_hid.forget(addr);
}

void BtManager::forget_all() {
    if (m_ready) m_hid.forget_all();
}

} // namespace picoboot
