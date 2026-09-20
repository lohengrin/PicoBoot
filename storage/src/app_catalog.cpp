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
    const auto files = sd_card.list_file_info({"bin"}, kMaxEntries, &m_truncated);
    m_entries.reserve(files.size());
    for (const auto& file : files) {
        m_entries.push_back(AppBinaryEntry{file.name, file.size});
    }

    std::sort(m_entries.begin(), m_entries.end(),
              [](const AppBinaryEntry& a, const AppBinaryEntry& b) { return a.filename < b.filename; });
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
