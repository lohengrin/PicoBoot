#include "picoboot/app_catalog.h"

#include <algorithm>

namespace picoboot {

void AppCatalog::refresh(const pico_toolset::SdCard& sd_card) {
    m_entries.clear();
    m_truncated = false;

    if (!sd_card.is_mounted()) {
        return;
    }

    // One directory pass gives names and sizes (hidden/system files and
    // "._x" sidecars are skipped); the old per-file stat() rescanned the
    // directory for every file, which is quadratic on big folders.
    std::vector<pico_toolset::SdCard::FileInfo> files;
    if (!sd_card.list_dir(m_cwd, {"bin"}, files, kMaxEntries, &m_truncated) && !m_cwd.empty()) {
        m_cwd.clear();
        sd_card.list_dir(m_cwd, {"bin"}, files, kMaxEntries, &m_truncated);
    }
    m_entries.reserve(files.size());
    for (auto& file : files) {
        std::string path = m_cwd.empty() ? file.name : m_cwd + "/" + file.name;
        m_entries.push_back(AppBinaryEntry{std::move(file.name), std::move(path), file.size, file.is_dir});
    }

    // Folders first, then files, each by name.
    std::sort(m_entries.begin(), m_entries.end(), [](const AppBinaryEntry& a, const AppBinaryEntry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;
        return a.filename < b.filename;
    });
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
