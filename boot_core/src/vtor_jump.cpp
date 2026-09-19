#include "picoboot/vtor_jump.h"

#include "picoboot/critical_section.h"
#include "picoboot/image_check.h"

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
