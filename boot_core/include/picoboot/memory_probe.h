#pragma once

#include <cstddef>

namespace picoboot {

// Stack / heap headroom of core 0, for diagnostics ("info" on the serial
// console). The stack is checked by painting its unused part with a pattern at
// startup and later counting how much is still untouched.

// Call once, early in main().
void stack_paint();

// Size of the core-0 stack and the bytes of it never used since stack_paint().
[[nodiscard]] size_t stack_total();
[[nodiscard]] size_t stack_unused();

// Bytes still available to malloc (free blocks plus the untouched heap area).
[[nodiscard]] size_t heap_free();

} // namespace picoboot
