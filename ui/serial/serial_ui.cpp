#include "serial_ui.h"

#include "picoboot/fastboot.h"
#include "picoboot/flash_layout.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace picoboot {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

constexpr int kBarWidth = 30;

} // namespace

SerialUi::SerialUi(AppManager& manager, bool allow_auto_boot)
    : m_manager(manager), m_allow_auto_boot(allow_auto_boot) {}

void SerialUi::add_command(const char* name, void (*handler)()) {
    if (m_command_count < kMaxCommands) m_commands[m_command_count++] = {name, handler};
}

void SerialUi::show_menu() {
    const AppCatalog& catalog = m_manager.catalog();
    const size_t count = catalog.count();
    const size_t pages = std::max<size_t>(1, (count + kPageSize - 1) / kPageSize);
    m_page = std::min(m_page, pages - 1);

    printf("\n=== PicoBoot ===\n");
    if (!m_manager.card_present()) {
        printf("  no \xC2\xB5SD card\n");
        printf("Actions: [r]efresh (retry mount)  [reboot]\n> ");
        fflush(stdout);
        return;
    }
    if (count == 0) {
        printf("  (no .bin files found)\n");
    } else {
        const size_t start = m_page * kPageSize;
        auto entries = catalog.page(start, kPageSize);
        for (size_t i = 0; i < entries.size(); ++i) {
            printf("%4zu. %s (%lu bytes)\n", start + i + 1, entries[i].filename.c_str(),
                   static_cast<unsigned long>(entries[i].size_bytes));
        }
        if (pages > 1) {
            printf("showing %zu-%zu over %zu\n", start + 1, start + entries.size(), count);
        }
    }

    printf("Actions:");
    if (count > 0) printf(" [1-%zu] load", count);
    if (m_page + 1 < pages) printf("  [n]ext page");
    if (m_page > 0) printf("  [p]revious page");
    printf("  [r]efresh  [reboot]  [bootsel]");
    for (size_t i = 0; i < m_command_count; ++i) printf("  [%s]", m_commands[i].name);
    printf("\n> ");
    fflush(stdout);
}

void SerialUi::print_progress(void*, float fraction) {
    const int filled = static_cast<int>(fraction * kBarWidth);
    char bar[kBarWidth + 1];
    for (int i = 0; i < kBarWidth; ++i) bar[i] = i < filled ? '=' : ' ';
    bar[kBarWidth] = '\0';
    printf("\r[%s] %3d%%", bar, static_cast<int>(fraction * 100));
    fflush(stdout);
}

void SerialUi::load(const AppBinaryEntry& entry) {
    printf("Loading '%s'...\n", entry.filename.c_str());
    fflush(stdout);
    const ProgressSink sink{print_progress, this};
    const LoadResult result = m_manager.load_and_boot(entry, sink);
    // load_and_boot() only returns on failure -- success reboots into the app.
    if (result != LoadResult::kBooting) {
        printf("\nError: %s\n", m_manager.last_error().c_str());
    }
}

void SerialUi::handle_line(const std::string& raw) {
    const std::string line = lower(raw);
    const size_t count = m_manager.catalog().count();
    const size_t pages = std::max<size_t>(1, (count + kPageSize - 1) / kPageSize);

    if (line.empty()) {
        // just redraw
    } else if (line == "reboot") {
        printf("Rebooting...\n");
        fflush(stdout);
        FastBoot::reboot_into_bootloader();
    } else if (line == "bootsel") {
        printf("Rebooting into BOOTSEL...\n");
        fflush(stdout);
        FastBoot::reboot_into_bootsel();
    } else if (line == "r") {
        printf("Refreshing...\n");
        m_manager.refresh();
        m_page = 0;
    } else if (line == "n") {
        if (m_page + 1 < pages) ++m_page;
    } else if (line == "p") {
        if (m_page > 0) --m_page;
    } else if (std::all_of(line.begin(), line.end(), [](unsigned char c) { return std::isdigit(c); })) {
        const unsigned long choice = strtoul(line.c_str(), nullptr, 10);
        const AppBinaryEntry* entry = choice >= 1 ? m_manager.catalog().find(choice - 1) : nullptr;
        if (entry) {
            load(*entry);
        } else {
            printf("No entry %s.\n", line.c_str());
        }
    } else {
        bool handled = false;
        for (size_t i = 0; i < m_command_count && !handled; ++i) {
            if (line == lower(m_commands[i].name)) {
                m_commands[i].handler();
                handled = true;
            }
        }
        if (!handled) printf("Unrecognized input '%s'.\n", raw.c_str());
    }
    show_menu();
}

void SerialUi::cancel_countdown() {
    if (!m_countdown_active) return;
    m_countdown_active = false;
    printf("\nAuto-boot cancelled.\n");
}

void SerialUi::update_countdown() {
    if (!m_countdown_active) return;

    const int64_t remaining_us = absolute_time_diff_us(get_absolute_time(), m_countdown_end);
    if (remaining_us <= 0) {
        m_countdown_active = false;
        printf("\n");
        load(*m_countdown_entry);
        show_menu();
        return;
    }
    const int seconds = static_cast<int>((remaining_us + 999'999) / 1'000'000);
    if (seconds != m_countdown_shown) {
        m_countdown_shown = seconds;
        printf("\rAuto-boot '%s' in %2d s (press any key to cancel)   ", m_countdown_entry->filename.c_str(),
               seconds);
        fflush(stdout);
    }
}

void SerialUi::poll() {
    if (!m_started) {
        m_started = true;
        m_manager.refresh();
        show_menu();

        const PicoBootConfig& cfg = m_manager.config();
        if (m_allow_auto_boot && cfg.auto_boot_timeout_s > 0 && !cfg.last_run_binary.empty()) {
            const AppCatalog& catalog = m_manager.catalog();
            for (size_t i = 0; i < catalog.count(); ++i) {
                if (catalog.find(i)->filename == cfg.last_run_binary) {
                    m_countdown_entry = catalog.find(i);
                    m_countdown_end = make_timeout_time_ms(cfg.auto_boot_timeout_s * 1000);
                    m_countdown_active = true;
                    printf("\n");
                    break;
                }
            }
        }
    }

    update_countdown();

    const int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) return;

    if (m_countdown_active) {
        cancel_countdown();
        show_menu();
        return; // the keypress only cancels
    }

    if (c == '\r' || c == '\n') {
        putchar('\n');
        std::string line;
        line.swap(m_line);
        handle_line(line);
    } else if ((c == 0x08 || c == 0x7F) && !m_line.empty()) {
        m_line.pop_back();
        printf("\b \b");
        fflush(stdout);
    } else if (c >= 0x20 && c < 0x7F && m_line.size() < 63) {
        m_line.push_back(static_cast<char>(c));
        putchar(c);
        fflush(stdout);
    }
}

} // namespace picoboot
