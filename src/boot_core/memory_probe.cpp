#include "picoboot/memory_probe.h"

#include "hardware/sync.h"

#include <cstdint>
#include <malloc.h>
#include <unistd.h>

extern "C" {
extern char __StackTop, __StackBottom, __HeapLimit;
}

namespace picoboot {

namespace {
uint8_t* g_region_base = nullptr; // set when main() runs on a stack of its own (stack_use_region)
size_t g_region_size = 0;
inline uint32_t* region_low() { return reinterpret_cast<uint32_t*>(g_region_base ? g_region_base : (uint8_t*)&__StackBottom); }
inline uint32_t* region_high() {
    return reinterpret_cast<uint32_t*>(g_region_base ? g_region_base + g_region_size : (uint8_t*)&__StackTop);
}
constexpr uint32_t kPattern = 0xDEADBEEFu;
constexpr size_t kLiveMargin = 128; // keep clear of the frames in use right now
} // namespace

void stack_use_region(void* base, size_t size) {
    g_region_base = static_cast<uint8_t*>(base);
    g_region_size = size;
}

void stack_paint() {
    auto* low = region_low();
    uint32_t marker;
    auto* high = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(&marker) - kLiveMargin);
    for (uint32_t* p = low; p < high; ++p) *p = kPattern;
}

size_t stack_total() { return static_cast<size_t>(reinterpret_cast<uint8_t*>(region_high()) - reinterpret_cast<uint8_t*>(region_low())); }

size_t stack_unused() {
    const uint32_t* p = region_low();
    const uint32_t* top = region_high();
    size_t words = 0;
    while (p + words < top && p[words] == kPattern) ++words;
    return words * sizeof(uint32_t);
}

size_t heap_free() {
    const struct mallinfo info = mallinfo();
    return static_cast<size_t>(&__HeapLimit - static_cast<char*>(sbrk(0))) + static_cast<size_t>(info.fordblks);
}

} // namespace picoboot
