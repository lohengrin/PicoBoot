#pragma once

#include <cstddef>
#include <cstdint>

namespace picoboot {

// The hard fault that ended the previous run, if any. Every PicoBoot target links the toolset's
// fault handler; a launched program that links it too stores its fault (pc/lr/cfsr in the
// watchdog scratch registers) and resets, which brings the board back into PicoBoot. A program
// without that handler halts in the SDK's default `bkpt` and cannot be reported.
//
// The scratch registers are consumed on the first call to crash_report(): call it once early in
// main() so the report is read before anything else touches them.
struct CrashReport {
    bool present = false;
    uint32_t pc = 0;
    uint32_t lr = 0;
    uint32_t cfsr = 0;
};

[[nodiscard]] const CrashReport& crash_report();

// Short name of the fault kind from the CFSR bits ("invalid instruction state", ...), "" if none known.
[[nodiscard]] const char* crash_cause(uint32_t cfsr);

// "Hard fault: <cause> at pc 0x... (lr 0x..., cfsr 0x...)" -- one line for a status bar / console.
size_t crash_summary(const CrashReport& report, char* out, size_t size);

} // namespace picoboot
