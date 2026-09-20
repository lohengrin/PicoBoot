#include "picoboot/config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace picoboot {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

} // namespace

bool PicoBootConfig::load() {
    FILE* f = fopen(kFilename, "r");
    if (!f) {
        return false;
    }

    char line[320]; // last_run is a path, up to AppCatalog::kMaxPath
    while (fgets(line, sizeof(line), f)) {
        std::string entry(line);
        const size_t eq = entry.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = trim(entry.substr(0, eq));
        const std::string value = trim(entry.substr(eq + 1));

        if (key == "last_run") {
            last_run_binary = value;
        } else if (key == "auto_boot_timeout") {
            auto_boot_timeout_s = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 10));
        }
    }

    fclose(f);
    return true;
}

bool PicoBootConfig::save() const {
    FILE* f = fopen(kFilename, "w");
    if (!f) {
        return false;
    }

    fprintf(f, "last_run=%s\n", last_run_binary.c_str());
    fprintf(f, "auto_boot_timeout=%lu\n", static_cast<unsigned long>(auto_boot_timeout_s));

    fclose(f);
    return true;
}

} // namespace picoboot
