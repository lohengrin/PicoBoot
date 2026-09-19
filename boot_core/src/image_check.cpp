#include "picoboot/image_check.h"

#include "hardware/regs/addressmap.h"

#include <cstring>

namespace picoboot {

bool looks_like_app_image(std::span<const uint8_t> image, uint32_t flash_base, uint32_t partition_size) {
    if (image.size() < kVectorTableOffset + 8) return false;

    uint32_t sp, reset;
    std::memcpy(&sp, image.data() + kVectorTableOffset, 4);
    std::memcpy(&reset, image.data() + kVectorTableOffset + 4, 4);

    const bool sp_in_sram = sp > SRAM_BASE && sp <= SRAM_END;
    const uint32_t reset_addr = reset & ~1u; // thumb bit
    const bool reset_in_app = reset_addr >= flash_base && reset_addr < flash_base + partition_size &&
                              (reset & 1u) != 0;
    return sp_in_sram && reset_in_app;
}

} // namespace picoboot
