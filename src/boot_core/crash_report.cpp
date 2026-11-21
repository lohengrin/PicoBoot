#include "picoboot/crash_report.h"

#include <cstdio>

#include "pico_toolset/fault_handler.h"

namespace picoboot {

const CrashReport& crash_report() {
    static CrashReport report;
    static bool captured = false;
    if (!captured) {
        captured = true;
        pico_toolset::FaultInfo info{};
        if (pico_toolset::consume_pending_hard_fault(info)) {
            report.present = true;
            report.pc = info.pc;
            report.lr = info.lr;
            report.cfsr = info.cfsr;
        }
    }
    return report;
}

const char* crash_cause(uint32_t cfsr) {
    // ARMv8-M CFSR: MMFSR bits 0-7, BFSR 8-15, UFSR 16-25. The most telling bit wins.
    struct Cause {
        uint32_t mask;
        const char* name;
    };
    static constexpr Cause kCauses[] = {
        {1u << 20, "stack overflow"},         {1u << 16, "undefined instruction"},
        {1u << 17, "invalid state"},          {1u << 18, "invalid PC load"},
        {1u << 19, "no coprocessor"},         {1u << 24, "unaligned access"},
        {1u << 25, "divide by zero"},         {1u << 0, "instruction access violation"},
        {1u << 1, "data access violation"},   {1u << 8, "instruction bus error"},
        {1u << 9, "precise bus error"},       {1u << 10, "imprecise bus error"},
    };
    for (const Cause& c : kCauses)
        if (cfsr & c.mask) return c.name;
    return "";
}

size_t crash_summary(const CrashReport& report, char* out, size_t size) {
    const char* cause = crash_cause(report.cfsr);
    const int n = snprintf(out, size, "Hard fault%s%s at pc 0x%08lX (lr 0x%08lX, cfsr 0x%08lX)", *cause ? ": " : "",
                           cause, static_cast<unsigned long>(report.pc), static_cast<unsigned long>(report.lr),
                           static_cast<unsigned long>(report.cfsr));
    return n < 0 ? 0 : static_cast<size_t>(n);
}

} // namespace picoboot
