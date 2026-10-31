#pragma once

#include "picoboot/bt_manager.h"

#include "pico_toolset/qwstpad.h"

#include "lvgl.h"

namespace picoboot {

// LVGL input devices for boards with Bluetooth HID and a QwSTPad (Presto):
//  - keypad: Bluetooth keyboard (arrows / Tab / Enter / Esc, Page Up / Down), Bluetooth gamepad
//    (D-pad, A / B / Start) and QwSTPad (D-pad, A = Enter, B = Esc, + = Page Down, - = Page Up)
//    drive LVGL's focus navigation on a default lv_group;
//  - pointer: Bluetooth mouse with a visible cursor (relative motion, wheel steps the focus).
// Creates a new lv_group and makes it the default: call BEFORE building the widgets that
// should be navigable. `pad` may be null; the manager's input is used once it is ready().
// Both objects must outlive the UI.
void lvgl_input_init(BtManager& bt, pico_toolset::QwstPad* pad, int width, int height);

// Puts the mouse cursor above everything on the top layer (call after creating an overlay there).
void lvgl_input_raise_cursor();

} // namespace picoboot
