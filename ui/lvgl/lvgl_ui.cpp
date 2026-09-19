#include "lvgl_ui.h"

#include "picoboot/fastboot.h"

#include "pico/stdlib.h"

#include <algorithm>
#include <cstdio>

namespace picoboot {

namespace {
constexpr size_t kMaxListEntries = 250;
constexpr int kHeaderHeightWide = 36;
constexpr int kHeaderHeightNarrow = 46; // title over subtitle
constexpr int kNarrowWidth = 400;
constexpr int kStatusHeight = 22;
} // namespace

struct LvglUiCallbacks {
    static void refresh(lv_event_t* e) { static_cast<LvglUi*>(lv_event_get_user_data(e))->m_pending = LvglUi::Pending::kRefresh; }
    static void reboot(lv_event_t* e) { static_cast<LvglUi*>(lv_event_get_user_data(e))->m_pending = LvglUi::Pending::kReboot; }
    static void select(lv_event_t* e) {
        auto* ui = static_cast<LvglUi*>(lv_event_get_user_data(e));
        ui->m_pending = LvglUi::Pending::kLoad;
        ui->m_pending_index = reinterpret_cast<size_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)));
    }
};

LvglUi::LvglUi(AppManager& manager, pico_toolset::LvglDisplayAdapter& adapter, bool allow_auto_boot)
    : m_manager(manager), m_adapter(adapter), m_allow_auto_boot(allow_auto_boot) {}

void LvglUi::build() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    const int w = lv_display_get_horizontal_resolution(m_adapter.display());
    const int h = lv_display_get_vertical_resolution(m_adapter.display());
    // Narrow canvases (e.g. the 320x240 HDMI one) stack the subtitle under
    // the title and use compact buttons so everything still fits one row.
    const bool narrow = w < kNarrowWidth;
    const int header_h = narrow ? kHeaderHeightNarrow : kHeaderHeightWide;
    const int btn_w = narrow ? 64 : 78;

    lv_obj_t* header = lv_obj_create(scr);
    lv_obj_set_size(header, w, header_h);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 6, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(header, 8, 0);

    lv_obj_t* title_parent = header;
    if (narrow) {
        title_parent = lv_obj_create(header);
        lv_obj_set_size(title_parent, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(title_parent, 0, 0);
        lv_obj_set_style_border_width(title_parent, 0, 0);
        lv_obj_set_style_bg_opa(title_parent, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(title_parent, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(title_parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(title_parent, 0, 0);
    }

    lv_obj_t* title = lv_label_create(title_parent);
    lv_label_set_text(title, "PicoBoot");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);

    lv_obj_t* subtitle = lv_label_create(title_parent);
    lv_label_set_text(subtitle, "by Lohengrin");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_palette_main(LV_PALETTE_GREY), 0);

    // Spacer pushing the buttons to the right.
    lv_obj_t* spacer = lv_obj_create(header);
    lv_obj_set_size(spacer, 1, 1);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t* btn_refresh = lv_button_create(header);
    lv_obj_set_size(btn_refresh, btn_w, 28);
    lv_obj_add_event_cb(btn_refresh, LvglUiCallbacks::refresh, LV_EVENT_CLICKED, this);
    lv_label_set_text(lv_label_create(btn_refresh), "Refresh");
    lv_obj_center(lv_obj_get_child(btn_refresh, 0));

    lv_obj_t* btn_reboot = lv_button_create(header);
    lv_obj_set_size(btn_reboot, btn_w, 28);
    lv_obj_add_event_cb(btn_reboot, LvglUiCallbacks::reboot, LV_EVENT_CLICKED, this);
    lv_label_set_text(lv_label_create(btn_reboot), "Reboot");
    lv_obj_center(lv_obj_get_child(btn_reboot, 0));

    m_list = lv_list_create(scr);
    lv_obj_set_size(m_list, w, h - header_h - kStatusHeight);
    lv_obj_set_pos(m_list, 0, header_h);
    lv_obj_set_style_radius(m_list, 0, 0);

    m_no_card = lv_label_create(scr);
    lv_label_set_text(m_no_card, "no uSD card");
    lv_obj_set_style_text_font(m_no_card, &lv_font_montserrat_20, 0);
    lv_obj_align(m_no_card, LV_ALIGN_CENTER, 0, 0);

    m_status = lv_label_create(scr);
    lv_obj_set_size(m_status, w - 8, kStatusHeight);
    lv_obj_set_pos(m_status, 4, h - kStatusHeight + 3);
    lv_label_set_long_mode(m_status, LV_LABEL_LONG_DOT);
    lv_label_set_text(m_status, "");

    m_bar = lv_bar_create(scr);
    lv_obj_set_size(m_bar, w - 8, 14);
    lv_obj_set_pos(m_bar, 4, h - kStatusHeight + 4);
    lv_bar_set_range(m_bar, 0, 100);
    lv_obj_add_flag(m_bar, LV_OBJ_FLAG_HIDDEN);

    m_built = true;
}

void LvglUi::populate() {
    lv_obj_clean(m_list);
    const bool present = m_manager.card_present();
    if (present) lv_obj_remove_flag(m_list, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(m_list, LV_OBJ_FLAG_HIDDEN);
    if (present) lv_obj_add_flag(m_no_card, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(m_no_card, LV_OBJ_FLAG_HIDDEN);
    if (!present) return;

    const AppCatalog& catalog = m_manager.catalog();
    const size_t shown = std::min(catalog.count(), kMaxListEntries);
    for (size_t i = 0; i < shown; ++i) {
        const AppBinaryEntry* entry = catalog.find(i);
        char text[96];
        snprintf(text, sizeof(text), "%s  (%lu KB)", entry->filename.c_str(),
                 static_cast<unsigned long>((entry->size_bytes + 1023) / 1024));
        lv_obj_t* btn = lv_list_add_button(m_list, LV_SYMBOL_FILE, text);
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(i));
        lv_obj_add_event_cb(btn, LvglUiCallbacks::select, LV_EVENT_CLICKED, this);
    }
    if (catalog.count() == 0) {
        lv_list_add_text(m_list, "(no .bin files found)");
    } else if (catalog.count() > shown) {
        lv_list_add_text(m_list, "(list truncated)");
    }
}

void LvglUi::set_status(const std::string& text, bool error) {
    lv_obj_add_flag(m_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(m_status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(m_status, error ? lv_palette_main(LV_PALETTE_RED) : lv_color_white(), 0);
    lv_label_set_text(m_status, text.c_str());
}

void LvglUi::start_countdown() {
    const PicoBootConfig& cfg = m_manager.config();
    if (!m_allow_auto_boot || cfg.auto_boot_timeout_s == 0 || cfg.last_run_binary.empty()) return;
    const AppCatalog& catalog = m_manager.catalog();
    for (size_t i = 0; i < catalog.count(); ++i) {
        if (catalog.find(i)->filename == cfg.last_run_binary) {
            m_countdown_index = i;
            m_countdown_ms = cfg.auto_boot_timeout_s * 1000;
            m_countdown_start_tick = lv_tick_get();
            m_countdown_shown = -1;
            m_countdown_active = true;
            return;
        }
    }
}

void LvglUi::update_countdown() {
    if (!m_countdown_active) return;
    const uint32_t elapsed = lv_tick_elaps(m_countdown_start_tick);
    // Any input on any indev cancels: the display's inactivity time drops
    // below the countdown's own age.
    if (lv_display_get_inactive_time(m_adapter.display()) + 50 < elapsed) {
        m_countdown_active = false;
        set_status("Auto-boot cancelled");
        return;
    }
    if (elapsed >= m_countdown_ms) {
        m_countdown_active = false;
        m_pending = Pending::kLoad;
        m_pending_index = m_countdown_index;
        return;
    }
    const int seconds = static_cast<int>((m_countdown_ms - elapsed + 999) / 1000);
    if (seconds != m_countdown_shown) {
        m_countdown_shown = seconds;
        char text[96];
        snprintf(text, sizeof(text), "Auto-boot '%s' in %d s (touch to cancel)",
                 m_manager.catalog().find(m_countdown_index)->filename.c_str(), seconds);
        set_status(text);
    }
}

void LvglUi::on_progress(void* ctx, float fraction) {
    auto* ui = static_cast<LvglUi*>(ctx);
    const int percent = static_cast<int>(fraction * 100);
    if (percent == ui->m_last_percent) return;
    ui->m_last_percent = percent;
    lv_bar_set_value(ui->m_bar, percent, LV_ANIM_OFF);
    ui->m_adapter.tick();
}

void LvglUi::load(size_t index) {
    const AppBinaryEntry* entry = m_manager.catalog().find(index);
    if (!entry) return;
    m_countdown_active = false;

    lv_obj_add_flag(m_status, LV_OBJ_FLAG_HIDDEN);
    lv_bar_set_value(m_bar, 0, LV_ANIM_OFF);
    lv_obj_remove_flag(m_bar, LV_OBJ_FLAG_HIDDEN);
    m_last_percent = -1;
    m_adapter.tick();

    const ProgressSink sink{on_progress, this};
    const LoadResult result = m_manager.load_and_boot(*entry, sink);
    if (result == LoadResult::kTooLarge) {
        set_status("Error: '" + entry->filename + "' does not fit in the application partition", true);
    } else if (result == LoadResult::kReadFailed) {
        set_status("Error: could not read '" + entry->filename + "'", true);
    } else if (result == LoadResult::kInvalidImage) {
        set_status("Error: '" + entry->filename + "' was not built for the application partition", true);
    } else if (result == LoadResult::kFlashFailed) {
        set_status("Error: flashing failed, application partition is not bootable", true);
    }
}

void LvglUi::poll() {
    if (!m_built) {
        build();
        m_manager.refresh();
        populate();
        start_countdown();
    }

    update_countdown();
    m_adapter.tick();

    // Actions run here, never inside LVGL event callbacks: loading calls
    // back into adapter.tick() for the progress bar, which must not nest
    // inside lv_timer_handler().
    const Pending pending = m_pending;
    m_pending = Pending::kNone;
    switch (pending) {
        case Pending::kRefresh:
            m_countdown_active = false;
            m_manager.refresh();
            populate();
            set_status(m_manager.card_present() ? "" : "no uSD card");
            break;
        case Pending::kReboot:
            FastBoot::reboot_into_bootloader();
        case Pending::kLoad:
            load(m_pending_index);
            break;
        case Pending::kNone:
            break;
    }
}

} // namespace picoboot

// Default LVGL assertion handler (see lv_conf.h's LV_ASSERT_HANDLER): report
// over CDC forever instead of hanging silently, keeping USB serviced through
// the adapter's idle hook. Targets may override it (the LCD target also
// paints the screen red).
extern "C" __attribute__((weak)) void picoboot_lvgl_assert(void) {
    while (true) {
        printf("LVGL assertion failed\n");
        for (absolute_time_t end = make_timeout_time_ms(1000); !time_reached(end);) {
            if (pico_toolset::LvglDisplayAdapter::s_idle_hook) pico_toolset::LvglDisplayAdapter::s_idle_hook();
        }
    }
}
