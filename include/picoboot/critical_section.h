#pragma once

#include "hardware/sync.h"

namespace picoboot {

// RAII interrupt-disable guard for critical sections the spec calls out
// explicitly: flash erase/program, VTOR relocation, stack-pointer changes,
// and the watchdog-tag reboot handoff. Plain save_and_disable_interrupts()/
// restore_interrupts() rather than a spinlock -- PicoBoot runs single-core
// through Phase 8, so there is no cross-core contention to arbitrate, only
// this core's own interrupt handlers to keep out of a half-done write.
class InterruptGuard {
public:
    InterruptGuard() : m_saved(save_and_disable_interrupts()) {}
    ~InterruptGuard() { restore_interrupts(m_saved); }

    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;

private:
    uint32_t m_saved;
};

} // namespace picoboot
