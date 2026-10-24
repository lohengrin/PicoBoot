#pragma once

// Per-board presets shared by the targets. The board is chosen at configure
// time (-DPICOBOOT_BOARD=..., see the top-level CMakeLists.txt), which
// defines exactly one PICOBOOT_BOARD_<NAME> macro.

#include "pico_toolset/sdcard.h"
#include "pico_toolset/sdcard_configs.h"

namespace picoboot::board {

inline const pico_toolset::SdCardConfig& sd_config() {
#if defined(PICOBOOT_BOARD_WAVESHARE_PIZERO)
    // The toolset preset clocks the card at 10 MHz; 15 MHz is reliable on this board's
    // PCB socket (20 MHz fails to mount), worth ~15% on USB mass-storage throughput.
    static const pico_toolset::SdCardConfig cfg = [] {
        pico_toolset::SdCardConfig c = pico_toolset::configs::sdcard::kWaveshareRp2350PiZero;
        c.clk_fast_hz = 15'000'000;
        return c;
    }();
    return cfg;
#elif defined(PICOBOOT_BOARD_CROWPANEL_PICO_HMI_28)
    return pico_toolset::configs::sdcard::kElecrowCrowPanelPicoHmi28;
#elif defined(PICOBOOT_BOARD_PRESTO)
    return pico_toolset::configs::sdcard::kPimoroniPresto;
#elif defined(PICOBOOT_BOARD_PICO_DV)
    return pico_toolset::configs::sdcard::kPicoDvCarrier;
#else
#error "no PICOBOOT_BOARD_* defined"
#endif
}

} // namespace picoboot::board
