#include "picoboot/bt_panel.h"

#include "picoboot/lvgl_input.h"

#include "pico/stdlib.h"

namespace picoboot {

namespace {
constexpr uint32_t kUpdateMs = 50;
}

BtPanel::BtPanel(BtManager& bt)
    : m_bt(bt),
      m_controller(bt.devices()),
      m_page(m_controller, {.title = LV_SYMBOL_BLUETOOTH " Bluetooth",
                            .title_font = &lv_font_montserrat_28,
                            .body_font = &lv_font_montserrat_20}) {}

void BtPanel::open() {
    if (m_overlay) return;
    const lv_display_t* display = lv_display_get_default();

    m_keypad = nullptr;
    for (lv_indev_t* i = lv_indev_get_next(nullptr); i; i = lv_indev_get_next(i)) {
        if (lv_indev_get_type(i) == LV_INDEV_TYPE_KEYPAD) m_keypad = i;
    }

    m_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(m_overlay, lv_display_get_horizontal_resolution(display), lv_display_get_vertical_resolution(display));
    lv_obj_set_pos(m_overlay, 0, 0);
    lv_obj_set_style_radius(m_overlay, 0, 0);
    lv_obj_set_style_border_width(m_overlay, 0, 0);
    lv_obj_set_style_pad_all(m_overlay, 0, 0);
    lv_obj_remove_flag(m_overlay, LV_OBJ_FLAG_SCROLLABLE);

    m_controller.open();
    m_page.create(m_overlay);
    lvgl_input_raise_cursor(); // the overlay was created above the mouse cursor
    if (m_keypad) {
        m_previous_group = lv_indev_get_group(m_keypad);
        lv_indev_set_group(m_keypad, m_page.group());
    }
}

void BtPanel::close() {
    if (!m_overlay) return;
    if (m_keypad) lv_indev_set_group(m_keypad, m_previous_group);
    m_page.destroy();
    lv_obj_delete(m_overlay);
    m_overlay = nullptr;
    m_previous_group = nullptr;
    m_keypad = nullptr;
    m_bt.devices().stop_scan();
}

void BtPanel::poll() {
    uint32_t code;
    if (m_open_requested || (!m_overlay && m_bt.ready() && m_bt.hid().pairing_passkey(&code))) open();
    m_open_requested = false;
    if (!m_overlay) return;

    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - m_last_update_ms < kUpdateMs) return;
    m_last_update_ms = now;
    m_page.update();
    if (m_controller.closed()) close();
}

} // namespace picoboot
