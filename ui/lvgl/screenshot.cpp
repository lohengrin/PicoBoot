#include "screenshot.h"

#include <cstdint>
#include <cstring>

namespace picoboot {

namespace {
void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
void put32(uint8_t* p, uint32_t v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
constexpr size_t kHeaderBytes = 54; // BITMAPFILEHEADER + BITMAPINFOHEADER
} // namespace

bool ScreenshotWriter::begin(const char* path, int width, int height) {
    m_file = fopen(path, "wb");
    if (!m_file) return false;
    m_width = width;
    m_height = height;
    m_row = 0;
    m_failed = false;

    const uint32_t row_bytes = (static_cast<uint32_t>(width) * 3 + 3) & ~3u;
    uint8_t h[kHeaderBytes]{};
    h[0] = 'B';
    h[1] = 'M';
    put32(h + 2, kHeaderBytes + row_bytes * height);
    put32(h + 10, kHeaderBytes);
    put32(h + 14, 40);                                // BITMAPINFOHEADER
    put32(h + 18, static_cast<uint32_t>(width));
    put32(h + 22, static_cast<uint32_t>(-height));    // negative: rows stored top to bottom
    put16(h + 26, 1);
    put16(h + 28, 24);
    put32(h + 34, row_bytes * height);
    if (fwrite(h, 1, sizeof(h), m_file) != sizeof(h)) m_failed = true;
    return true;
}

void ScreenshotWriter::on_tile(void* ctx, const lv_area_t* area, const uint16_t* pixels) {
    auto* self = static_cast<ScreenshotWriter*>(ctx);
    if (!self->m_file || self->m_failed) return;
    const int tile_w = area->x2 - area->x1 + 1;
    // Only full-width tiles arriving in order can be streamed.
    if (area->x1 != 0 || tile_w != self->m_width || area->y1 != self->m_row) {
        self->m_failed = true;
        return;
    }

    const uint32_t row_bytes = (static_cast<uint32_t>(self->m_width) * 3 + 3) & ~3u;
    static uint8_t row[(480 + 3) * 3 + 4]; // widest canvas is 480; static: keeps the flush stack small
    if (row_bytes > sizeof(row)) {
        self->m_failed = true;
        return;
    }
    std::memset(row, 0, sizeof(row));
    for (int y = area->y1; y <= area->y2 && y < self->m_height; ++y) {
        uint8_t* out = row;
        for (int x = 0; x < tile_w; ++x) {
            const uint16_t v = pixels[x];
            const uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
            *out++ = static_cast<uint8_t>((b << 3) | (b >> 2));
            *out++ = static_cast<uint8_t>((g << 2) | (g >> 4));
            *out++ = static_cast<uint8_t>((r << 3) | (r >> 2));
        }
        pixels += tile_w;
        if (fwrite(row, 1, row_bytes, self->m_file) != row_bytes) {
            self->m_failed = true;
            return;
        }
        ++self->m_row;
    }
}

bool ScreenshotWriter::end() {
    bool ok = m_file != nullptr && !m_failed && complete();
    if (m_file && fclose(m_file) != 0) ok = false;
    m_file = nullptr;
    return ok;
}

} // namespace picoboot
