// Phase 1 skeleton: proves the build system, the offset-linker-script
// target pattern, and boot_core's link (FastBoot/reset_buttons) before any
// real SD/USB/flash logic exists. The file list below is hardcoded; it is
// replaced by storage::AppCatalog in Phase 2.

#include "picoboot/fastboot.h"

#include "pico/stdlib.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace {

constexpr const char* kFakeFiles[] = {"demo_a.bin", "demo_b.bin"};

void print_menu() {
    printf("\nPicoBoot (Phase 1 skeleton)\n");
    for (size_t i = 0; i < std::size(kFakeFiles); ++i) {
        printf("  %zu. %s\n", i + 1, kFakeFiles[i]);
    }
    printf("Enter a number to load, 'r' to refresh, 'reboot' to reboot.\n> ");
    fflush(stdout);
}

} // namespace

int main() {
    // First thing, before any peripheral init: check whether we're coming
    // back from a fast-boot-tagged watchdog reboot. On this Phase 1
    // skeleton nothing ever sets kBootApp yet (no real flashing exists),
    // so this always falls through to full init -- but the check itself,
    // and its link against pico_toolset_reset_buttons, is exercised here
    // from day one rather than bolted on later.
    picoboot::BootTag tag;
    if (picoboot::FastBoot::consume(tag)) {
        // Only kBootApp would skip init; nothing produces it yet in
        // Phase 1, so any tag observed here just falls through below.
    }

    stdio_init_all();
    sleep_ms(2000); // let the host's CDC enumerate before the first printf

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
                printf("Refresh: nothing to rescan yet (Phase 1 skeleton).\n");
            } else {
                int choice = atoi(line);
                if (choice >= 1 && static_cast<size_t>(choice) <= std::size(kFakeFiles)) {
                    printf("Would load '%s' (flashing not implemented until Phase 3).\n",
                           kFakeFiles[choice - 1]);
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
