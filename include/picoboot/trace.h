#pragma once

// Boot timeline and main-loop latency statistics, for tuning how quickly the USB drive becomes
// ready and how long anything keeps the USB stack from being serviced. Compiled in with
// -DPICOBOOT_USB_STATS=ON (printed by the 'stats' serial command); otherwise every call here is an
// empty inline stub. Header-only and free of any other PicoBoot dependency, so every layer can use it.

#include <cstdint>

#ifdef PICOBOOT_USB_STATS
#include "pico/time.h"

#include <cstdio>
#include <cstring>
#endif

namespace picoboot::trace {

#ifdef PICOBOOT_USB_STATS

namespace detail {

struct Mark {
    const char* name;
    uint64_t at_us;
};
struct ScopeStat {
    const char* name;
    uint32_t count;
    uint64_t total_us;
    uint32_t max_us;
};
constexpr size_t kMaxMarks = 24;
constexpr size_t kMaxScopes = 20;
constexpr uint32_t kGapEdgesUs[] = {1000, 2000, 5000, 10000, 20000, 50000, 100000};
constexpr size_t kGapBuckets = sizeof(kGapEdgesUs) / sizeof(kGapEdgesUs[0]) + 1;

inline Mark g_marks[kMaxMarks];
inline size_t g_mark_count = 0;
inline ScopeStat g_scopes[kMaxScopes];
inline size_t g_scope_count = 0;
inline uint64_t g_reset_us = 0; // when print() last reset the counters: older samples are dropped
inline uint32_t g_gap_hist[kGapBuckets];
inline uint32_t g_gap_max_us = 0;
inline uint64_t g_gap_max_at_us = 0;
inline uint64_t g_gap_total_us = 0;
inline uint32_t g_gap_count = 0;

} // namespace detail

// Boot timeline: remembers when `name` happened (microseconds since boot).
inline void mark(const char* name) {
    if (detail::g_mark_count < detail::kMaxMarks) detail::g_marks[detail::g_mark_count++] = {name, time_us_64()};
}

// Time between two consecutive usb_bridge_task() calls.
inline void gap(uint64_t us) {
    using namespace detail;
    if (time_us_64() - us < g_reset_us) return; // started before the last reset (e.g. the printing itself)
    size_t bucket = 0;
    while (bucket < kGapBuckets - 1 && us >= kGapEdgesUs[bucket]) ++bucket;
    ++g_gap_hist[bucket];
    ++g_gap_count;
    g_gap_total_us += us;
    if (us > g_gap_max_us) {
        g_gap_max_us = static_cast<uint32_t>(us);
        g_gap_max_at_us = time_us_64();
    }
}

// Times a section that can keep the main loop away from the USB stack (one that does not pump
// USB itself). Per name: calls, total and worst duration.
class Scope {
public:
    explicit Scope(const char* name) : m_name(name), m_start(time_us_64()) {}
    ~Scope() {
        using namespace detail;
        if (m_start < g_reset_us) return; // began before the last reset (the 'stats' command's own scope)
        const uint64_t us = time_us_64() - m_start;
        ScopeStat* stat = nullptr;
        for (size_t i = 0; i < g_scope_count; ++i) {
            if (g_scopes[i].name == m_name || strcmp(g_scopes[i].name, m_name) == 0) {
                stat = &g_scopes[i];
                break;
            }
        }
        if (!stat && g_scope_count < kMaxScopes) {
            stat = &g_scopes[g_scope_count++];
            *stat = {m_name, 0, 0, 0};
        }
        if (!stat) return;
        ++stat->count;
        stat->total_us += us;
        if (us > stat->max_us) stat->max_us = static_cast<uint32_t>(us);
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    const char* m_name;
    uint64_t m_start;
};

// 'stats' serial command: the boot timeline, then the latency statistics since the previous call.
inline void print() {
    using namespace detail;
    printf("Boot timeline (ms since boot):\n");
    uint64_t previous = 0;
    for (size_t i = 0; i < g_mark_count; ++i) {
        printf("  %7.1f  +%6.1f  %s\n", g_marks[i].at_us / 1000.0, (g_marks[i].at_us - previous) / 1000.0, g_marks[i].name);
        previous = g_marks[i].at_us;
    }
    printf("Gap between usb_bridge_task() calls: %lu calls, avg %llu us, max %lu us (at %.1f s)\n",
           static_cast<unsigned long>(g_gap_count), static_cast<unsigned long long>(g_gap_count ? g_gap_total_us / g_gap_count : 0),
           static_cast<unsigned long>(g_gap_max_us), g_gap_max_at_us / 1e6);
    printf("  histogram:");
    for (size_t i = 0; i < kGapBuckets; ++i) {
        if (i + 1 < kGapBuckets) printf(" <%lums:%lu", static_cast<unsigned long>(kGapEdgesUs[i] / 1000), static_cast<unsigned long>(g_gap_hist[i]));
        else printf(" >=100ms:%lu\n", static_cast<unsigned long>(g_gap_hist[i]));
    }
    printf("Timed sections (not pumping USB themselves):\n");
    for (size_t i = 0; i < g_scope_count; ++i) {
        printf("  %-14s %6lu calls  total %8.1f ms  max %7.1f ms\n", g_scopes[i].name, static_cast<unsigned long>(g_scopes[i].count),
               g_scopes[i].total_us / 1000.0, g_scopes[i].max_us / 1000.0);
    }
    memset(g_gap_hist, 0, sizeof(g_gap_hist));
    g_gap_max_us = 0;
    g_gap_total_us = 0;
    g_gap_count = 0;
    g_scope_count = 0;
    g_reset_us = time_us_64();
}

#else

inline void mark(const char*) {}
inline void gap(uint64_t) {}
inline void print() {}
class Scope {
public:
    explicit Scope(const char*) {}
};

#endif

} // namespace picoboot::trace
