#include "picoboot/bt_manager.h"

#include "pico/cyw43_arch.h"

namespace picoboot {

bool BtManager::init() {
    // No printing in here: the caller may be servicing USB from an interrupt meanwhile.
    if (cyw43_arch_init() != 0) return false;
    if (!m_bt.init(pico_toolset::BluetoothConfig{.name = "PicoBoot"})) return false;
    pico_toolset::BluetoothHidConfig config;
    config.auto_pair = false;      // the Bluetooth page lists what it finds and the user picks
    config.enable_classic = false; // BLE only: keyboards, mice and gamepads that speak HID over GATT
    if (!m_hid.init(config)) return false;
    m_devices.add_backend(m_backend);
#ifdef PICOBOOT_BT_A2DP
    pico_toolset::A2dpConfig a2dp;
    a2dp.local_name = "PicoBoot";
    a2dp.auto_connect = false;   // the page lists the headsets found and the user picks
    a2dp.auto_reconnect = false; // no background paging of an absent headset
    a2dp.auto_play = false;      // no audio is ever sent
    if (m_a2dp.init(a2dp)) m_devices.add_backend(m_a2dp_backend);
#endif
    m_ready = true;
    return true;
}

} // namespace picoboot
