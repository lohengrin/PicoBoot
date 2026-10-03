#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace picoboot {

// Offset from an app's flash base to its real vector table. pico-sdk only
// force-links a .boot2 stub ahead of the vector table on RP2040 (256 bytes,
// exactly); RP2350 has none -- confirmed on real built .bin files of both
// chips (RP2350: vector table at +0x0; RP2040: SP 0x20042000 / reset
// 0x100801f7 at +0x100).
#if PICO_RP2350
inline constexpr uint32_t kVectorTableOffset = 0x0u;
#else
inline constexpr uint32_t kVectorTableOffset = 0x100u;
#endif

enum class ImageChip { kUnknown, kRp2040, kRp2350 };

#if PICO_RP2350
inline constexpr ImageChip kBoardChip = ImageChip::kRp2350;
#else
inline constexpr ImageChip kBoardChip = ImageChip::kRp2040;
#endif

const char* chip_name(ImageChip chip);

enum class ImageCheck {
    kOk,
    kTooSmall,          // shorter than the header that has to be inspected
    kUnknownChip,       // neither an RP2040 boot stage nor an RP2350 image definition
    kWrongChip,         // built for the other chip family
    kRiscV,             // RP2350 RISC-V image (this bootloader runs Arm)
    kBadVectors,        // stack pointer not in SRAM / reset vector not in flash
    kUnsupportedLayout, // linked for 0x10000000 and this chip cannot remap flash
};

enum class ImageLayout {
    kPartition, // linked at the application partition: runs from where it is flashed
    kFlashBase, // a normal build (linked at 0x10000000): needs flash address translation (RP2350)
};

struct ImageInfo {
    ImageCheck result = ImageCheck::kOk;
    ImageChip chip = ImageChip::kUnknown;
    ImageLayout layout = ImageLayout::kPartition;
};

// Bytes from the start of the file that inspect_image() needs.
inline constexpr size_t kImageHeadBytes = 4096;

// Works out, from the first kImageHeadBytes of a raw .bin (no metadata needed):
//  - the chip family: an RP2040 image starts with a 256-byte boot stage
//    ending in its CRC32; an RP2350 image carries a picobin block (start
//    marker 0xFFFFDED3) in its first 4 KiB, whose image-type item names the
//    chip and CPU;
//  - where it was linked, from the reset vector: inside the application
//    partition, or at the flash base (a normal build).
// A result other than kOk says why the image cannot run here.
[[nodiscard]] ImageInfo inspect_image(std::span<const uint8_t> head, uint32_t partition_base,
                                      uint32_t partition_size);

} // namespace picoboot
