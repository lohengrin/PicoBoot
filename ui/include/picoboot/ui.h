#pragma once

namespace picoboot {

// Common interface of the UI backends (serial today, LVGL later). Backends
// only talk to AppManager; they never touch flash/VTOR/watchdog.
class BootUi {
public:
    virtual ~BootUi() = default;

    // Non-blocking; call from the main loop. Drives input handling,
    // rendering and the auto-boot countdown.
    virtual void poll() = 0;
};

} // namespace picoboot
