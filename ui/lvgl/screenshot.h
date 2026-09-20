#pragma once

#include "lvgl.h"

#include <cstdio>

namespace picoboot {

// Collects the tiles LVGL renders (LvglDisplayAdapter's flush tap) into a 24-bit BMP on the
// card, streamed row by row: no framebuffer copy is needed, so it works with the partial
// render buffer of every backend. Tiles must arrive top to bottom at full width, which is
// how LVGL renders a full-screen invalidation.
class ScreenshotWriter {
public:
    // Opens `path` and writes the header. False if the file cannot be created.
    bool begin(const char* path, int width, int height);
    static void on_tile(void* ctx, const lv_area_t* area, const uint16_t* pixels);
    // Closes the file. True if every row was written without error.
    bool end();
    [[nodiscard]] bool complete() const { return m_row >= m_height; }

private:
    FILE* m_file = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_row = 0;
    bool m_failed = false;
};

} // namespace picoboot
