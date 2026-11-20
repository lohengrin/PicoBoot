#include "picoboot/app_catalog.h"

#include "ff.h"
#include "pico/time.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace picoboot {

struct AppCatalog::Scan {
    DIR dir;
};

namespace {

bool is_bin(const char* name) {
    const char* dot = strrchr(name, '.');
    return dot && tolower(static_cast<unsigned char>(dot[1])) == 'b' && tolower(static_cast<unsigned char>(dot[2])) == 'i' &&
           tolower(static_cast<unsigned char>(dot[3])) == 'n' && dot[4] == '\0';
}

constexpr uint32_t kSliceUs = 2000; // blocking refresh: USB is served at least this often

} // namespace

AppCatalog::AppCatalog() = default;
AppCatalog::~AppCatalog() { finish_scan(); }

void AppCatalog::finish_scan() {
    if (!m_scan) return;
    f_closedir(&m_scan->dir);
    m_scan.reset();
}

void AppCatalog::begin_refresh(const pico_toolset::SdCard& sd_card) {
    finish_scan();
    m_entries.clear();
    m_truncated = false;
    if (!sd_card.is_mounted()) return;

    // One directory pass gives names and sizes (hidden/system files and "._x" sidecars are skipped);
    // a per-file stat() would rescan the directory for every file, which is quadratic on big folders.
    auto scan = std::make_unique<Scan>();
    if (f_opendir(&scan->dir, m_cwd.c_str()) != FR_OK) {
        if (m_cwd.empty()) return;
        m_cwd.clear(); // the folder is gone (deleted by the host, card swapped): back to the root
        if (f_opendir(&scan->dir, "") != FR_OK) return;
    }
    m_scan = std::move(scan);
}

bool AppCatalog::step(uint32_t budget_us) {
    if (!m_scan) return false;
    const absolute_time_t deadline = make_timeout_time_us(budget_us);
    FILINFO info;
    while (f_readdir(&m_scan->dir, &info) == FR_OK && info.fname[0] != '\0') {
        if ((info.fattrib & (AM_HID | AM_SYS)) || info.fname[0] == '.') {
            // hidden / system / sidecar files are not listed
        } else {
            const bool is_dir = (info.fattrib & AM_DIR) != 0;
            if (is_dir || is_bin(info.fname)) {
                if (m_entries.size() >= kMaxEntries) {
                    m_truncated = true;
                    break;
                }
                std::string name(info.fname);
                std::string path = m_cwd.empty() ? name : m_cwd + "/" + name;
                m_entries.push_back(AppBinaryEntry{std::move(name), std::move(path),
                                                   is_dir ? 0u : static_cast<uint32_t>(info.fsize), is_dir});
            }
        }
        if (time_reached(deadline)) return true;
    }
    finish_scan();

    // Folders first, then files, each by name.
    std::sort(m_entries.begin(), m_entries.end(), [](const AppBinaryEntry& a, const AppBinaryEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;
        return a.filename < b.filename;
    });
    return false;
}

void AppCatalog::refresh(const pico_toolset::SdCard& sd_card) {
    begin_refresh(sd_card);
    while (step(kSliceUs)) {
        if (m_yield) m_yield();
    }
}

bool AppCatalog::enter(size_t index) {
    const AppBinaryEntry* entry = find(index);
    if (!entry || !entry->is_dir || entry->path.size() > kMaxPath) return false;
    m_cwd = entry->path;
    return true;
}

bool AppCatalog::up(std::string* left) {
    if (m_cwd.empty()) return false;
    const size_t slash = m_cwd.rfind('/');
    if (left) *left = slash == std::string::npos ? m_cwd : m_cwd.substr(slash + 1);
    m_cwd = slash == std::string::npos ? std::string() : m_cwd.substr(0, slash);
    return true;
}

std::span<const AppBinaryEntry> AppCatalog::page(size_t start, size_t count) const {
    if (start >= m_entries.size()) {
        return {};
    }
    const size_t end = std::min(start + count, m_entries.size());
    return std::span<const AppBinaryEntry>(m_entries).subspan(start, end - start);
}

const AppBinaryEntry* AppCatalog::find(size_t index) const {
    return index < m_entries.size() ? &m_entries[index] : nullptr;
}

} // namespace picoboot
