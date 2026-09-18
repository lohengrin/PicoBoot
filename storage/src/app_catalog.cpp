#include "picoboot/app_catalog.h"

#include <algorithm>
#include <sys/stat.h>

namespace picoboot {

void AppCatalog::refresh(const pico_toolset::SdCard& sd_card) {
    m_entries.clear();

    if (!sd_card.is_mounted()) {
        return;
    }

    // list_files() only returns names (root-dir only, no subdirectories --
    // fine for PicoBoot's flat catalog); sizes come from stat(), which
    // needs PICO_TOOLSET_SDCARD_STDIO linked in (see storage/CMakeLists.txt).
    for (const std::string& name : sd_card.list_files({"bin"})) {
        struct stat st{};
        uint32_t size = 0;
        if (::stat(name.c_str(), &st) == 0) {
            size = static_cast<uint32_t>(st.st_size);
        }
        m_entries.push_back(AppBinaryEntry{name, size});
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
