#pragma once

// Per-board presets shared by the targets. The board is chosen at configure
// time (-DPICOBOOT_BOARD=..., see the top-level CMakeLists.txt), which
// defines exactly one PICOBOOT_BOARD_<NAME> macro.

#include "pico_toolset/sdcard.h"
#include "pico_toolset/sdcard_configs.h"

namespace picoboot::board {

inline const pico_toolset::SdCardConfig& sd_config() {
#if defined(PICOBOOT_BOARD_WAVESHARE_PIZERO)
    return pico_toolset::configs::sdcard::kWaveshareRp2350PiZero;
#elif defined(PICOBOOT_BOARD_CROWPANEL_PICO_HMI_28)
    return pico_toolset::configs::sdcard::kElecrowCrowPanelPicoHmi28;
#elif defined(PICOBOOT_BOARD_PICO_DV)
    return pico_toolset::configs::sdcard::kPicoDvCarrier;
#else
#error "no PICOBOOT_BOARD_* defined"
#endif
}

} // namespace picoboot::board
