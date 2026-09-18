#include "picoboot/vtor_jump.h"

#include "picoboot/critical_section.h"

// RP2350's Cortex-M33 has an architectural SCB->VTOR (ARMv8-M). RP2040's
// Cortex-M0+ has no architectural VTOR, but RP2040 silicon exposes a
// proprietary relocation register at the same offset via the M0PLUS/PPB
// struct -- same effect, different header/struct name per chip.
#if PICO_RP2350
#include "hardware/structs/scb.h"
#else
#include "hardware/structs/m0plus.h"
#endif

namespace picoboot {

namespace {

// Offset from an app's flash base to its real vector table.
// pico-sdk's pico_standard_link only force-links a .boot2 stub ahead of
// the vector table on RP2040 (a real, always-256-byte QSPI-setup stub
// that the on-chip ROM jumps into on a cold boot from flash offset 0);
// RP2350 has no such requirement (its .boot2 section is optional and, in
// a build that never references it, is discarded entirely) and no ROM
// concept of chain-loading from a non-zero flash offset in the first
// place -- confirmed on real hardware for RP2350 by inspecting a built
// testapps/app_blink.bin: its actual vector table (a plausible RAM SP
// followed by a matching reset handler address) sits at +0x0, not +0x100.
// Since neither chip's app is ever cold-booted by the on-chip ROM at this
// offset anyway (VTOR relocation is always a warm jump from the
// already-running bootloader), the RP2040 branch's boot2 bytes, if
// present, are simply unused padding either way -- this constant only
// needs to match wherever the linker actually put the vector table.
// TODO(Phase 8): re-verify this the same way on a real RP2040 build
// before relying on it there.
#if PICO_RP2350
constexpr uint32_t kVectorTableOffset = 0x0u;
#else
constexpr uint32_t kVectorTableOffset = 0x100u;
#endif

} // namespace

void relocate_vtor_and_jump(uint32_t app_flash_base) {
    InterruptGuard guard;

    // Standard Cortex-M vector table layout: word 0 is the initial MSP,
    // word 1 is the reset handler address (already thumb-bit-tagged by
    // the compiler that built the app, so it's used as-is in BX below).
    const uint32_t vector_table = app_flash_base + kVectorTableOffset;
    const uint32_t* vtor = reinterpret_cast<const uint32_t*>(vector_table);
    const uint32_t app_sp = vtor[0];
    const uint32_t app_reset = vtor[1];

#if PICO_RP2350
    scb_hw->vtor = vector_table;
#else
    ppb_hw->vtor = vector_table;
#endif

    __asm volatile(
        "msr msp, %0 \n"
        "bx  %1 \n"
        :
        : "r"(app_sp), "r"(app_reset)
        :);

    __builtin_unreachable();
}

} // namespace picoboot
