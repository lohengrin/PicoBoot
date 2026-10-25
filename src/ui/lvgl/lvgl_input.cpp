#include "picoboot/lvgl_input.h"

#include <algorithm>

namespace picoboot {

namespace {

// HID keyboard usage IDs.
constexpr uint8_t kKeyEnter = 0x28, kKeyEsc = 0x29, kKeyTab = 0x2B, kKeyKeypadEnter = 0x58;
constexpr uint8_t kKeyRight = 0x4F, kKeyLeft = 0x50, kKeyDown = 0x51, kKeyUp = 0x52;
constexpr uint8_t kKeyPageUp = 0x4B, kKeyPageDown = 0x4E;

constexpr int kPageRows = 10;      // focus steps of a Page Up / Down
constexpr int kMouseSpeedNum = 3;  // pointer pixels per mouse count: 3/2
constexpr int kMouseSpeedDen = 2;

struct State {
    BtManager* bt = nullptr;
    pico_toolset::QwstPad* pad = nullptr;
    int width = 0, height = 0;

    // Focus steps still to send for a page jump or a wheel notch (+ next, - previous).
    int queued = 0;
    bool release_pending = false; // the key just sent still needs its release event
    uint16_t last_pad_buttons = 0;

    int mouse_x = 0, mouse_y = 0;
    lv_obj_t* cursor = nullptr;
};
State g;

// Arrows navigate via PREV/NEXT: LVGL only moves group focus on those; raw UP/DOWN would
// scroll the parent list instead.
uint32_t held_key() {
    namespace pt = pico_toolset;
    uint32_t key = 0;
    if (g.bt->ready()) {
        pt::BluetoothHid& hid = g.bt->hid();
        if (hid.is_key_down(kKeyUp) || hid.is_key_down(kKeyLeft)) key = LV_KEY_PREV;
        else if (hid.is_key_down(kKeyDown) || hid.is_key_down(kKeyRight) || hid.is_key_down(kKeyTab)) key = LV_KEY_NEXT;
        else if (hid.is_key_down(kKeyEnter) || hid.is_key_down(kKeyKeypadEnter)) key = LV_KEY_ENTER;
        else if (hid.is_key_down(kKeyEsc)) key = LV_KEY_ESC;

        if (key == 0) {
            const pt::GamepadState pad = hid.gamepad_state(0);
            if (pad.present) {
                if (pad.down(pt::kBtUp) || pad.down(pt::kBtLeft)) key = LV_KEY_PREV;
                else if (pad.down(pt::kBtDown) || pad.down(pt::kBtRight)) key = LV_KEY_NEXT;
                else if (pad.down(pt::kBtA) || pad.down(pt::kBtStart)) key = LV_KEY_ENTER;
                else if (pad.down(pt::kBtB)) key = LV_KEY_ESC;
            }
        }
    }
    return key;
}

// Page jumps, mouse wheel and the QwSTPad's own buttons, all edge-triggered.
void collect_queued_steps() {
    namespace qb = pico_toolset::qwstpad_button;
    if (g.bt->ready()) {
        pico_toolset::BluetoothHid& hid = g.bt->hid();
        if (hid.consume_key_press(kKeyPageDown)) g.queued += kPageRows;
        if (hid.consume_key_press(kKeyPageUp)) g.queued -= kPageRows;
        g.queued -= hid.consume_mouse_wheel(); // wheel up (positive) = towards the top
    }
    if (g.pad) {
        const uint16_t now = g.pad->read_buttons();
        const uint16_t pressed = now & ~g.last_pad_buttons;
        g.last_pad_buttons = now;
        if (pressed & qb::Plus) g.queued += kPageRows;
        if (pressed & qb::Minus) g.queued -= kPageRows;
    }
}

uint32_t pad_held_key() {
    namespace qb = pico_toolset::qwstpad_button;
    const uint16_t b = g.last_pad_buttons;
    if (b & (qb::Up | qb::Left)) return LV_KEY_PREV;
    if (b & (qb::Down | qb::Right)) return LV_KEY_NEXT;
    if (b & qb::A) return LV_KEY_ENTER;
    if (b & qb::B) return LV_KEY_ESC;
    return 0;
}

void keypad_read_cb(lv_indev_t*, lv_indev_data_t* data) {
    // A queued step is a press followed by a release, one event per call.
    if (g.release_pending) {
        g.release_pending = false;
        data->state = LV_INDEV_STATE_RELEASED;
        data->continue_reading = g.queued != 0;
        return;
    }
    collect_queued_steps();
    if (g.queued != 0) {
        data->key = g.queued > 0 ? LV_KEY_NEXT : LV_KEY_PREV;
        g.queued += g.queued > 0 ? -1 : 1;
        g.release_pending = true;
        data->state = LV_INDEV_STATE_PRESSED;
        data->continue_reading = true;
        return;
    }

    uint32_t key = held_key();
    if (key == 0) key = pad_held_key();
    data->key = key;
    data->state = key != 0 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void mouse_read_cb(lv_indev_t*, lv_indev_data_t* data) {
    data->state = LV_INDEV_STATE_RELEASED;
    const bool present = g.bt->ready() && g.bt->hid().mouse_state().present;
    if (g.cursor) {
        if (present) lv_obj_remove_flag(g.cursor, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(g.cursor, LV_OBJ_FLAG_HIDDEN);
    }
    if (!present) return;

    int dx = 0, dy = 0;
    g.bt->hid().consume_mouse_delta(dx, dy);
    g.mouse_x = std::clamp(g.mouse_x + dx * kMouseSpeedNum / kMouseSpeedDen, 0, g.width - 1);
    g.mouse_y = std::clamp(g.mouse_y + dy * kMouseSpeedNum / kMouseSpeedDen, 0, g.height - 1);
    data->point.x = g.mouse_x;
    data->point.y = g.mouse_y;
    const auto m = g.bt->hid().mouse_state();
    data->state = (m.left || m.right || m.middle) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

} // namespace

void lvgl_input_init(BtManager& bt, pico_toolset::QwstPad* pad, int width, int height) {
    g.bt = &bt;
    g.pad = pad;
    g.width = width;
    g.height = height;
    g.mouse_x = width / 2;
    g.mouse_y = height / 2;

    lv_group_t* group = lv_group_create();
    lv_group_set_default(group);

    lv_indev_t* keypad = lv_indev_create();
    lv_indev_set_type(keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(keypad, keypad_read_cb);
    lv_indev_set_group(keypad, group);

    lv_indev_t* mouse = lv_indev_create();
    lv_indev_set_type(mouse, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(mouse, mouse_read_cb);

    // Crosshair cursor, centred on the hit point (LVGL anchors a cursor's top-left at the
    // pointer); hidden until a mouse is connected.
    g.cursor = lv_label_create(lv_layer_top());
    lv_label_set_text(g.cursor, "+");
    lv_obj_set_style_text_color(g.cursor, lv_color_white(), 0);
    lv_obj_set_style_text_font(g.cursor, &lv_font_montserrat_20, 0);
    lv_obj_update_layout(g.cursor);
    lv_obj_set_style_translate_x(g.cursor, -(lv_obj_get_width(g.cursor) / 2), 0);
    lv_obj_set_style_translate_y(g.cursor, -(lv_obj_get_height(g.cursor) / 2), 0);
    lv_obj_add_flag(g.cursor, LV_OBJ_FLAG_HIDDEN);
    lv_indev_set_cursor(mouse, g.cursor);
}

} // namespace picoboot
