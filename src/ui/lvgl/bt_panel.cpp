#include "picoboot/bt_panel.h"

#include "pico/stdlib.h"

#include <cstdio>
#include <cstring>

namespace picoboot {

struct BtPanelCallbacks {
    static void close(lv_event_t* e) { static_cast<BtPanel*>(lv_event_get_user_data(e))->m_close_requested = true; }
    static void pair(lv_event_t* e) { static_cast<BtPanel*>(lv_event_get_user_data(e))->m_pair_toggle_requested = true; }
    static void forget(lv_event_t* e) {
        auto* panel = static_cast<BtPanel*>(lv_event_get_user_data(e));
        panel->m_forget_requested = static_cast<int>(reinterpret_cast<size_t>(lv_obj_get_user_data(lv_event_get_target_obj(e))));
    }
};

namespace {

constexpr uint32_t kListRefreshMs = 1000; // connection state changes without a toolset event too

const char* kinds_text(uint8_t kinds, char* out, size_t size) {
    out[0] = 0;
    auto add = [&](const char* word) {
        if (out[0]) strncat(out, ", ", size - strlen(out) - 1);
        strncat(out, word, size - strlen(out) - 1);
    };
    if (kinds & pico_toolset::bt_hid_kind::Keyboard) add("keyboard");
    if (kinds & pico_toolset::bt_hid_kind::Mouse) add("mouse");
    if (kinds & pico_toolset::bt_hid_kind::Gamepad) add("gamepad");
    return out;
}

lv_obj_t* add_button(lv_obj_t* parent, const char* text, int width, lv_event_cb_t cb, void* user, lv_obj_t** label_out = nullptr) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_size(btn, width, 36);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    if (label_out) *label_out = label;
    return btn;
}

} // namespace

void BtPanel::open() {
    if (m_overlay) return;
    const lv_display_t* display = lv_display_get_default();
    const int w = lv_display_get_horizontal_resolution(display);
    const int h = lv_display_get_vertical_resolution(display);

    // The panel's own focus group, made the keypad's for as long as it is open.
    m_keypad = nullptr;
    for (lv_indev_t* i = lv_indev_get_next(nullptr); i; i = lv_indev_get_next(i)) {
        if (lv_indev_get_type(i) == LV_INDEV_TYPE_KEYPAD) m_keypad = i;
    }
    lv_group_t* default_group = lv_group_get_default();
    m_group = lv_group_create();
    lv_group_set_default(m_group);
    if (m_keypad) {
        m_previous_group = lv_indev_get_group(m_keypad);
        lv_indev_set_group(m_keypad, m_group);
    }

    m_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(m_overlay, w, h);
    lv_obj_set_pos(m_overlay, 0, 0);
    lv_obj_set_style_radius(m_overlay, 0, 0);
    lv_obj_set_style_border_width(m_overlay, 0, 0);
    lv_obj_set_style_pad_all(m_overlay, 12, 0);
    lv_obj_remove_flag(m_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(m_overlay);
    lv_label_set_text(title, LV_SYMBOL_BLUETOOTH " Bluetooth");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    m_status = lv_label_create(m_overlay);
    lv_obj_set_width(m_status, w - 24);
    lv_label_set_long_mode(m_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(m_status, &lv_font_montserrat_20, 0);
    lv_obj_align(m_status, LV_ALIGN_TOP_LEFT, 0, 48);

    m_passkey = lv_label_create(m_overlay);
    lv_obj_set_style_text_font(m_passkey, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(m_passkey, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_set_style_text_letter_space(m_passkey, 6, 0);
    lv_obj_align(m_passkey, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_add_flag(m_passkey, LV_OBJ_FLAG_HIDDEN);

    m_list = lv_obj_create(m_overlay);
    lv_obj_set_size(m_list, w - 24, h - 24 - 150 - 56);
    lv_obj_align(m_list, LV_ALIGN_TOP_LEFT, 0, 150);
    lv_obj_set_flex_flow(m_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(m_list, 4, 0);
    lv_obj_set_style_pad_row(m_list, 4, 0);

    lv_obj_t* buttons = lv_obj_create(m_overlay);
    lv_obj_set_size(buttons, w - 24, 44);
    lv_obj_align(buttons, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_pad_all(buttons, 0, 0);
    lv_obj_set_style_border_width(buttons, 0, 0);
    lv_obj_set_style_bg_opa(buttons, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    add_button(buttons, "PAIR NEW", 160, BtPanelCallbacks::pair, this, &m_pair_label);
    add_button(buttons, "CLOSE", 160, BtPanelCallbacks::close, this);

    lv_group_set_default(default_group); // later widgets belong to the main screen's group
    m_shown_revision = ~0u;               // force the first refresh
    m_last_list_ms = 0;
}

void BtPanel::close() {
    if (!m_overlay) return;
    if (m_keypad) lv_indev_set_group(m_keypad, m_previous_group);
    lv_obj_delete(m_overlay); // also drops its widgets from the group
    lv_group_delete(m_group);
    m_overlay = m_status = m_passkey = m_list = m_pair_label = nullptr;
    m_group = m_previous_group = nullptr;
    m_keypad = nullptr;
    m_bt.stop_pairing();
}

void BtPanel::rebuild_list(bool force) {
    pico_toolset::BtHidDevice now[pico_toolset::BluetoothHid::kMaxBonded];
    const size_t count = m_bt.bonded(now, pico_toolset::BluetoothHid::kMaxBonded);
    // Rebuilding drops the keypad focus: only do it when a row actually changes (the RSSI
    // of a device being scanned is not shown).
    bool same = count == m_device_count;
    for (size_t i = 0; same && i < count; ++i) {
        same = memcmp(now[i].addr, m_devices[i].addr, 6) == 0 && now[i].kinds == m_devices[i].kinds &&
               now[i].connected == m_devices[i].connected && strcmp(now[i].name, m_devices[i].name) == 0;
    }
    if (same && !force) return;
    for (size_t i = 0; i < count; ++i) m_devices[i] = now[i];
    m_device_count = count;

    lv_obj_clean(m_list);
    if (m_device_count == 0) {
        lv_label_set_text(lv_label_create(m_list), "No paired devices");
        return;
    }
    lv_group_t* previous_default = lv_group_get_default();
    lv_group_set_default(m_group);
    for (size_t i = 0; i < m_device_count; ++i) {
        const pico_toolset::BtHidDevice& device = m_devices[i];
        lv_obj_t* row = lv_obj_create(m_list);
        lv_obj_set_size(row, lv_pct(100), 44);
        lv_obj_set_style_pad_all(row, 4, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        char kinds[40];
        char text[96];
        snprintf(text, sizeof(text), "%s  %s", device.name[0] ? device.name : "(unnamed)",
                 kinds_text(device.kinds, kinds, sizeof(kinds)));
        lv_obj_t* label = lv_label_create(row);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, device.connected ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_width(label, lv_pct(70));
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

        lv_obj_t* forget = lv_button_create(row);
        lv_obj_set_size(forget, 64, 30);
        lv_obj_set_user_data(forget, reinterpret_cast<void*>(i));
        lv_obj_add_event_cb(forget, BtPanelCallbacks::forget, LV_EVENT_CLICKED, this);
        lv_label_set_text(lv_label_create(forget), "Forget");
        lv_obj_center(lv_obj_get_child(forget, 0));
    }
    lv_group_set_default(previous_default);
}

void BtPanel::refresh(const BtManager::Snapshot& s) {
    char text[160];
    bool show_passkey = false;
    if (!s.ready) {
        snprintf(text, sizeof(text), "Bluetooth is not available (the radio did not start).");
    } else {
        switch (s.phase) {
            case BtManager::Phase::kScanning:
                snprintf(text, sizeof(text), "Searching: put the keyboard, mouse or gamepad in pairing mode.");
                break;
            case BtManager::Phase::kConnecting:
                snprintf(text, sizeof(text), "Connecting to %s...", s.device_name[0] ? s.device_name : "device");
                break;
            case BtManager::Phase::kPasskey:
                snprintf(text, sizeof(text), "Type this code on %s, then press Enter:",
                         s.device_name[0] ? s.device_name : "the keyboard");
                show_passkey = true;
                break;
            case BtManager::Phase::kPaired:
                snprintf(text, sizeof(text), "Paired with %s, finishing...", s.device_name[0] ? s.device_name : "device");
                break;
            case BtManager::Phase::kFailed:
                snprintf(text, sizeof(text), "Pairing failed (status 0x%02X, reason 0x%02X). Tap PAIR NEW to retry.",
                         s.fail_status, s.fail_reason);
                break;
            case BtManager::Phase::kIdle:
            default:
                if (s.result == BtManager::Result::kConnected) {
                    snprintf(text, sizeof(text), "Connected: %s", s.device_name[0] ? s.device_name : "device");
                } else if (s.result == BtManager::Result::kTimedOut) {
                    snprintf(text, sizeof(text), "No device found. Tap PAIR NEW to try again.");
                } else {
                    snprintf(text, sizeof(text), "Tap PAIR NEW, then put the device in pairing mode.");
                }
                break;
        }
    }
    lv_label_set_text(m_status, text);

    if (show_passkey) {
        char code[16];
        snprintf(code, sizeof(code), "%06lu", static_cast<unsigned long>(s.passkey));
        lv_label_set_text(m_passkey, code);
        lv_obj_remove_flag(m_passkey, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_passkey, LV_OBJ_FLAG_HIDDEN);
    }
    const bool pairing = s.phase != BtManager::Phase::kIdle && s.phase != BtManager::Phase::kFailed;
    lv_label_set_text(m_pair_label, pairing ? "STOP" : "PAIR NEW");
}

void BtPanel::poll() {
    const BtManager::Snapshot snapshot = m_bt.snapshot();
    const uint32_t now = to_ms_since_boot(get_absolute_time());

    if (m_open_requested || (!m_overlay && snapshot.phase == BtManager::Phase::kPasskey)) open();
    m_open_requested = false;
    if (!m_overlay) return;

    if (m_close_requested) {
        m_close_requested = false;
        close();
        return;
    }
    if (m_pair_toggle_requested) {
        m_pair_toggle_requested = false;
        const bool pairing = snapshot.phase != BtManager::Phase::kIdle && snapshot.phase != BtManager::Phase::kFailed;
        if (pairing) m_bt.stop_pairing(); else m_bt.start_pairing();
    }
    if (m_forget_requested >= 0) {
        const int index = m_forget_requested;
        m_forget_requested = -1;
        if (static_cast<size_t>(index) < m_device_count) m_bt.forget(m_devices[index].addr);
        m_shown_revision = ~0u;
    }

    if (snapshot.revision != m_shown_revision || snapshot.phase != m_shown_phase) {
        m_shown_revision = snapshot.revision;
        m_shown_phase = snapshot.phase;
        refresh(snapshot);
        rebuild_list(true);
        m_last_list_ms = now;
    } else if (now - m_last_list_ms >= kListRefreshMs) {
        m_last_list_ms = now;
        rebuild_list(false);
    }
}

} // namespace picoboot
