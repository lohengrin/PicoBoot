#include "picoboot/vtor_jump.h"

#include "picoboot/critical_section.h"
#include "picoboot/image_check.h"

// RP2350's Cortex-M33 has an architectural SCB->VTOR (ARMv8-M). RP2040's
// Cortex-M0+ has no architectural VTOR, but RP2040 silicon exposes a
// proprietary relocation register at the same offset via the M0PLUS/PPB
// struct -- same effect, different header/struct name per chip.
#if PICO_RP2350
#include "hardware/structs/qmi.h"
#include "hardware/structs/scb.h"
#include "hardware/xip_cache.h"
#include "pico/platform.h"
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

    // The application expects the state a real reset would give it, in
    // particular PRIMASK=0: this function runs under an InterruptGuard, and
    // nothing has enabled an interrupt since the (watchdog) reset that led
    // here, so there is nothing pending that could reach the app's handlers
    // before it initialises. MSP first, then unmask, then branch.
    __asm volatile(
        "msr msp, %0 \n"
        "cpsie i \n"
        "bx  %1 \n"
        :
        : "r"(app_sp), "r"(app_reset)
        :);

    __builtin_unreachable();
}

#if PICO_RP2350
namespace {

constexpr uint32_t kPagesPerWindow = 1024; // one ATRANS window = 4 MiB = 1024 x 4 KiB

// Runs entirely from RAM (see jump_to_app_remapped()).
[[noreturn]] void __no_inline_not_in_flash_func(remap_and_jump)(uint32_t sp, uint32_t reset, uint32_t first_page,
                                                                  uint32_t pages) {
    // Window i covers virtual 4 MiB * i; physical = BASE (4 KiB units) + offset in the window.
    // Chain windows so the partition looks like one contiguous image at 0x10000000.
    for (uint32_t i = 0; i < 4 && pages > 0; ++i) {
        const uint32_t size = pages > kPagesPerWindow ? kPagesPerWindow : pages;
        qmi_hw->atrans[i] = (size << QMI_ATRANS0_SIZE_LSB) |
                            ((first_page + i * kPagesPerWindow) & QMI_ATRANS0_BASE_BITS);
        pages -= size;
    }
    __dsb();
    __isb();
    xip_cache_invalidate_all(); // the XIP cache is virtually addressed

    scb_hw->vtor = XIP_BASE; // the application's vector table is now at the start of the window
    __asm volatile(
        "msr msp, %0 \n"
        "cpsie i \n"
        "bx  %1 \n"
        :
        : "r"(sp), "r"(reset)
        :);
    __builtin_unreachable();
}

} // namespace

void jump_to_app_remapped(uint32_t app_flash_base, uint32_t partition_bytes) {
    InterruptGuard guard;

    // Read the application's vector table through its *physical* location,
    // before the mapping changes (RP2350: the table is at offset 0).
    const uint32_t* vtor = reinterpret_cast<const uint32_t*>(app_flash_base);
    const uint32_t app_sp = vtor[0];
    const uint32_t app_reset = vtor[1];

    remap_and_jump(app_sp, app_reset, (app_flash_base - XIP_BASE) / 4096u, partition_bytes / 4096u);
}
#endif

} // namespace picoboot
