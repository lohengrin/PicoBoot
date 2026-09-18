// Phase 3: real flash-writer/VTOR-jump/fast-boot round trip. Still a bare
// menu (no paging/auto-boot yet -- that's Phase 5's job).

#include "picoboot/app_catalog.h"
#include "picoboot/app_manager.h"
#include "picoboot/config.h"
#include "picoboot/fastboot.h"

#include "pico_toolset/sdcard.h"
#include "pico_toolset/sdcard_configs.h"

#include "pico/stdlib.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

pico_toolset::SdCard g_sd_card;
picoboot::AppCatalog g_catalog;
picoboot::PicoBootConfig g_config;
picoboot::AppManager g_app_manager(g_sd_card, g_config);

void print_progress(void*, float fraction) {
    printf("\r  flashing... %3d%%", static_cast<int>(fraction * 100));
    fflush(stdout);
}

void print_menu() {
    printf("\nPicoBoot (Phase 2 skeleton)\n");
    if (!g_sd_card.is_mounted()) {
        printf("  no \xC2\xB5SD card\n");
    } else if (g_catalog.count() == 0) {
        printf("  (no .bin files found)\n");
    } else {
        auto entries = g_catalog.page(0, g_catalog.count());
        for (size_t i = 0; i < entries.size(); ++i) {
            printf("  %zu. %s (%lu bytes)\n", i + 1, entries[i].filename.c_str(),
                   static_cast<unsigned long>(entries[i].size_bytes));
        }
    }
    printf("last_run=%s auto_boot_timeout=%lus\n", g_config.last_run_binary.c_str(),
           static_cast<unsigned long>(g_config.auto_boot_timeout_s));
    printf("Enter a number to load, 'r' to refresh, 'reboot' to reboot.\n> ");
    fflush(stdout);
}

void refresh() {
    if (!g_sd_card.is_mounted()) {
        g_sd_card.init(pico_toolset::configs::sdcard::kWaveshareRp2350PiZero);
    }
    g_catalog.refresh(g_sd_card);
    if (g_sd_card.is_mounted()) {
        g_config.load();
    }
}

} // namespace

int main() {
    // First thing, before any peripheral init: check whether we're coming
    // back from a fast-boot-tagged watchdog reboot (see boot_core's
    // FastBoot doc comment). Nothing produces kBootApp yet in this phase
    // (no real flashing exists), so this always falls through to full init.
    picoboot::BootTag tag;
    picoboot::FastBoot::consume(tag);

    stdio_init_all();
    sleep_ms(2000); // let the host's CDC enumerate before the first printf

    refresh();
    print_menu();

    char line[64];
    size_t pos = 0;
    while (true) {
        int c = getchar();
        if (c == '\r' || c == '\n') {
            line[pos] = '\0';
            putchar('\n');

            if (pos == 0) {
                // ignore bare newline
            } else if (strcmp(line, "reboot") == 0) {
                printf("Rebooting...\n");
                fflush(stdout);
                picoboot::FastBoot::reboot_into_bootloader();
            } else if (line[0] == 'r' && line[1] == '\0') {
                printf("Refreshing...\n");
                refresh();
            } else {
                int choice = atoi(line);
                const picoboot::AppBinaryEntry* entry =
                    choice >= 1 ? g_catalog.find(static_cast<size_t>(choice - 1)) : nullptr;
                if (entry) {
                    printf("Loading '%s'...\n", entry->filename.c_str());
                    fflush(stdout);
                    picoboot::ProgressSink sink{print_progress, nullptr};
                    const picoboot::LoadResult result = g_app_manager.load_and_boot(*entry, sink);
                    // load_and_boot() only returns on failure -- success
                    // reboots into the app and never comes back here.
                    switch (result) {
                        case picoboot::LoadResult::kTooLarge:
                            printf("\nError: '%s' is too large for the app partition.\n",
                                   entry->filename.c_str());
                            break;
                        case picoboot::LoadResult::kReadFailed:
                            printf("\nError: failed to read '%s' from the SD card.\n",
                                   entry->filename.c_str());
                            break;
                        case picoboot::LoadResult::kBooting:
                            break; // unreachable
                    }
                } else {
                    printf("Unrecognized input '%s'.\n", line);
                }
            }
            print_menu();
            pos = 0;
        } else if (pos + 1 < sizeof(line)) {
            line[pos++] = static_cast<char>(c);
            putchar(c);
        }
    }
}
