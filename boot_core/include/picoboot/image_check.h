#pragma once

#include <cstdint>
#include <span>

namespace picoboot {

// Offset from an app's flash base to its real vector table. pico-sdk only
// force-links a .boot2 stub ahead of the vector table on RP2040 (256 bytes,
// exactly); RP2350 has none -- confirmed on a real built RP2350 .bin, its
// vector table sits at +0x0. TODO(Phase 8): verify the RP2040 value the
// same way before relying on it.
#if PICO_RP2350
inline constexpr uint32_t kVectorTableOffset = 0x0u;
#else
inline constexpr uint32_t kVectorTableOffset = 0x100u;
#endif

// Cheap sanity check that `image` was linked for `flash_base`: the initial
// stack pointer must point into SRAM and the reset handler into the
// application partition. Catches the classic mistake of copying an
// application built for the default 0x10000000 layout. It does NOT (and
// cannot, from a raw .bin) tell RP2040 builds from RP2350 ones.
[[nodiscard]] bool looks_like_app_image(std::span<const uint8_t> image, uint32_t flash_base,
                                        uint32_t partition_size);

} // namespace picoboot
