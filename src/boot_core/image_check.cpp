#include "picoboot/image_check.h"

#include "hardware/regs/addressmap.h"

#include <cstring>

namespace picoboot {

namespace {

constexpr uint32_t kPicobinStart = 0xFFFFDED3u;
constexpr uint32_t kPicobinEnd = 0xAB123579u;
constexpr uint32_t kItemImageType = 0x42u; // 1-word item: {0x42, size=1, image type (16 bits)}
constexpr uint32_t kItemLast = 0xFFu;      // 0x80 | 0x7f: 2-byte-size "last item" marker

// image type bits (boot/picobin.h)
constexpr uint32_t kTypeMask = 0x000Fu, kTypeExe = 0x1u;
constexpr uint32_t kCpuShift = 8, kCpuMask = 0x7u, kCpuArm = 0u, kCpuRiscV = 1u;
constexpr uint32_t kChipShift = 12, kChipMask = 0x7u, kChipRp2350 = 1u;

uint32_t rd32(std::span<const uint8_t> data, size_t offset) {
    uint32_t value;
    std::memcpy(&value, data.data() + offset, 4);
    return value;
}

// CRC-32/MPEG-2 (poly 0x04C11DB7, init 0xFFFFFFFF, no reflection): what the
// RP2040 bootrom checks over the first 252 bytes of the boot stage.
uint32_t crc32_mpeg2(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint32_t>(data[i]) << 24;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : (crc << 1);
        }
    }
    return crc;
}

// Returns the picobin image-type word (16 bits) of an RP2350 image, or 0 if
// the first 4 KiB hold no image definition.
uint32_t find_rp2350_image_type(std::span<const uint8_t> head) {
    const size_t limit = head.size() < kImageHeadBytes ? head.size() : kImageHeadBytes;
    for (size_t start = 0; start + 4 <= limit; start += 4) {
        if (rd32(head, start) != kPicobinStart) continue;

        for (size_t p = start + 4; p + 4 <= limit;) {
            const uint32_t word = rd32(head, p);
            if (word == kPicobinEnd) break;
            const uint32_t type = word & 0xFFu;
            const uint32_t size = (type & 0x80u) ? (word >> 8) & 0xFFFFu : (word >> 8) & 0xFFu; // in words
            if (size == 0) break;
            if (type == kItemImageType) return (word >> 16) & 0xFFFFu;
            if (type == kItemLast) break;
            p += static_cast<size_t>(size) * 4;
        }
    }
    return 0;
}

} // namespace

const char* chip_name(ImageChip chip) {
    switch (chip) {
        case ImageChip::kRp2040: return "RP2040";
        case ImageChip::kRp2350: return "RP2350";
        default: return "an unknown chip";
    }
}

ImageInfo inspect_image(std::span<const uint8_t> head, uint32_t partition_base, uint32_t partition_size) {
    ImageInfo info;

    // --- chip family ------------------------------------------------------
    bool riscv = false;
    const uint32_t image_type = find_rp2350_image_type(head);
    if (image_type != 0) {
        const bool exe = (image_type & kTypeMask) == kTypeExe;
        const uint32_t chip = (image_type >> kChipShift) & kChipMask;
        riscv = ((image_type >> kCpuShift) & kCpuMask) == kCpuRiscV;
        if (exe && chip == kChipRp2350) info.chip = ImageChip::kRp2350;
    } else if (head.size() >= 256 && crc32_mpeg2(head.data(), 252) == rd32(head, 252)) {
        info.chip = ImageChip::kRp2040;
    }

    if (info.chip == ImageChip::kUnknown) {
        info.result = head.size() < 256 ? ImageCheck::kTooSmall : ImageCheck::kUnknownChip;
        return info;
    }
    if (info.chip != kBoardChip) {
        info.result = ImageCheck::kWrongChip;
        return info;
    }
    if (riscv) {
        info.result = ImageCheck::kRiscV;
        return info;
    }

    // --- where it was linked ---------------------------------------------
    if (head.size() < kVectorTableOffset + 8) {
        info.result = ImageCheck::kTooSmall;
        return info;
    }
    const uint32_t sp = rd32(head, kVectorTableOffset);
    const uint32_t reset = rd32(head, kVectorTableOffset + 4);
    const uint32_t reset_addr = reset & ~1u;

    const bool sp_in_sram = sp > SRAM_BASE && sp <= SRAM_END;
    const bool thumb = (reset & 1u) != 0;
    if (!sp_in_sram || !thumb) {
        info.result = ImageCheck::kBadVectors;
        return info;
    }

    if (reset_addr >= partition_base && reset_addr < partition_base + partition_size) {
        info.layout = ImageLayout::kPartition;
    } else if (reset_addr >= XIP_BASE && reset_addr < partition_base) {
        info.layout = ImageLayout::kFlashBase;
#if !PICO_RP2350
        info.result = ImageCheck::kUnsupportedLayout; // RP2040 cannot translate flash addresses
#endif
    } else {
        info.result = ImageCheck::kBadVectors;
    }
    return info;
}

} // namespace picoboot
