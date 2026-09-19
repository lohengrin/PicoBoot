#pragma once

#include "picoboot/app_manager.h"
#include "picoboot/ui.h"

#include "pico/stdlib.h"

#include <string>

namespace picoboot {

// Text UI over stdio (CDC): numbered, paged list; prompt with the available
// actions printed before each prompt; ASCII progress bar; auto-boot
// countdown of the last-run binary (cancelled by any key).
class SerialUi : public BootUi {
public:
    static constexpr size_t kPageSize = 20;

    // allow_auto_boot=false when arriving from an app's reboot-to-bootloader
    // request (else an app that always returns would loop forever).
    SerialUi(AppManager& manager, bool allow_auto_boot);

    void poll() override;

private:
    void show_menu();
    void handle_line(const std::string& line);
    void load(const AppBinaryEntry& entry);
    void update_countdown();
    void cancel_countdown();
    static void print_progress(void* ctx, float fraction);

    AppManager& m_manager;
    size_t m_page = 0;
    std::string m_line;
    bool m_started = false;

    bool m_countdown_active = false;
    absolute_time_t m_countdown_end{};
    int m_countdown_shown = -1;
    const AppBinaryEntry* m_countdown_entry = nullptr;
    bool m_allow_auto_boot;
};

} // namespace picoboot
