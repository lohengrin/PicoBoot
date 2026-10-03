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
constexpr uint32_t kPattern = 0xDEADBEEFu;
constexpr size_t kLiveMargin = 128; // keep clear of the frames in use right now
} // namespace

void stack_paint() {
    auto* low = reinterpret_cast<uint32_t*>(&__StackBottom);
    uint32_t marker;
    auto* high = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(&marker) - kLiveMargin);
    for (uint32_t* p = low; p < high; ++p) *p = kPattern;
}

size_t stack_total() { return static_cast<size_t>(&__StackTop - &__StackBottom); }

size_t stack_unused() {
    const auto* p = reinterpret_cast<const uint32_t*>(&__StackBottom);
    const auto* top = reinterpret_cast<const uint32_t*>(&__StackTop);
    size_t words = 0;
    while (p + words < top && p[words] == kPattern) ++words;
    return words * sizeof(uint32_t);
}

size_t heap_free() {
    const struct mallinfo info = mallinfo();
    return static_cast<size_t>(&__HeapLimit - static_cast<char*>(sbrk(0))) + static_cast<size_t>(info.fordblks);
}

} // namespace picoboot
