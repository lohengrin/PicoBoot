#pragma once

#include "picoboot/app_manager.h"
#include "picoboot/ui.h"

#include "pico_toolset/lvgl_display.h"

#include <string>

namespace picoboot {

// LVGL UI: header row ("PicoBoot" / smaller "by Lohengrin" plus Refresh and
// Reboot buttons, kept in the header so the list keeps the vertical space),
// list view of the catalog, and a status row that shows the auto-boot
// countdown, errors, or the flashing progress bar. Backend-agnostic: the
// display/touch come from a pico_toolset::LvglDisplayAdapter set up by the
// target.
class LvglUi : public BootUi {
public:
    LvglUi(AppManager& manager, pico_toolset::LvglDisplayAdapter& adapter, bool allow_auto_boot);

    void poll() override;

    // Saves the screen as the next screenshot_NNNN.bmp in the card root (done from poll()).
    void request_screenshot() { m_screenshot_requested = true; }

private:
    enum class Pending { kNone, kRefresh, kReboot, kLoad, kEnter, kUp };

    void build();
    void populate();
    void set_status(const std::string& text, bool error = false);
    void start_countdown();
    void update_countdown();
    void load(size_t index);
    void navigate(bool up, size_t index);
    void take_screenshot();
    static void on_progress(void* ctx, float fraction);

    AppManager& m_manager;
    pico_toolset::LvglDisplayAdapter& m_adapter;
    bool m_built = false;

    lv_obj_t* m_list = nullptr;
    lv_obj_t* m_no_card = nullptr;
    lv_obj_t* m_status = nullptr;
    lv_obj_t* m_bar = nullptr;

    bool m_screenshot_requested = false;
    Pending m_pending = Pending::kNone;
    size_t m_pending_index = 0;
    std::string m_reselect; // folder just left: scrolled into view after going up
    int m_last_percent = -1;

    bool m_allow_auto_boot;
    bool m_countdown_active = false;
    uint32_t m_countdown_start_tick = 0;
    uint32_t m_countdown_ms = 0;
    int m_countdown_shown = -1;
    size_t m_countdown_index = 0;

    friend struct LvglUiCallbacks;
};

} // namespace picoboot
