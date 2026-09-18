# PicoBoot architecture

See [`PicoBootloaderV2.md`](../PicoBootloaderV2.md) for the full specification this implements.

## Layers

```
targets/*  (compile-time UI selection: picoboot_serial, picoboot_lvgl_lcd, picoboot_lvgl_dvi)
    |
    v
ui  (BootUi interface: SerialUi, LvglUi)
    |
    v
app_manager  (orchestrates: load_and_boot(), refresh_catalog(), reboot())
    |
    +----------------+----------------+
    v                v                v
storage          config          boot_core
(AppCatalog)   (PicoBootConfig)  (FlashWriter, VtorJump, FastBoot)
    |                |                |
    v                v                v
pico_toolset::SdCard (read/write .cfg, list/read .bin)   pico_toolset::reset_buttons
                                                          (watchdog_reboot_with_tag /
                                                           consume_pending_watchdog_tag)

usb  (UsbBridge: MSC block device <-> SdCard raw diskio, CDC)
  -- runs independently alongside ui's poll loop, no dependency edge to app_manager
```

Dependencies flow strictly downward. `boot_core` never depends on SD/USB/UI — it
operates only on flash addresses and byte spans.

## Component diagram

```mermaid
graph TD
    subgraph targets
        T1[picoboot_serial]
        T2[picoboot_lvgl_lcd]
        T3[picoboot_lvgl_dvi]
    end

    subgraph ui
        BootUi[["BootUi «interface»"]]
        SerialUi
        LvglUi
    end

    AppManager[app_manager::AppManager]
    Catalog[storage::AppCatalog]
    Config[config::PicoBootConfig]
    BootCore["boot_core: FlashWriter / VtorJump / FastBoot"]

    SdCard[pico_toolset::SdCard]
    ResetButtons["pico_toolset::watchdog_reboot_with_tag /\nconsume_pending_watchdog_tag"]

    UsbBridge[usb::UsbBridge]
    UsbComposite["pico_toolset_usb_composite\n(MSC + CDC, new toolset component)"]

    LvglAdapter["pico_toolset_lvgl_display::LvglDisplayAdapter\n(new toolset component)"]
    DisplayPanel[["pico_toolset::DisplayPanel «interface»"]]
    DviFb[pico_toolset_dvi_hdmi framebuffer]
    Touch[pico_toolset::TouchPanel]
    Hid[pico_toolset_usb_hid::UsbHidHost]

    T1 --> SerialUi
    T2 --> LvglUi
    T3 --> LvglUi
    SerialUi -.realizes.-> BootUi
    LvglUi -.realizes.-> BootUi

    BootUi --> AppManager
    AppManager --> Catalog
    AppManager --> Config
    AppManager --> BootCore

    Catalog --> SdCard
    Config --> SdCard
    BootCore --> ResetButtons

    UsbBridge --> UsbComposite
    UsbComposite --> SdCard

    LvglUi --> LvglAdapter
    LvglAdapter --> DisplayPanel
    LvglAdapter --> DviFb
    LvglAdapter --> Touch
    LvglAdapter --> Hid
```

## Class diagram (key classes)

```mermaid
classDiagram
    class BootUi {
        <<interface>>
        +poll()
        +render_catalog(AppCatalog&)
        +render_no_card()
        +render_progress(float)
    }
    class SerialUi
    class LvglUi
    BootUi <|.. SerialUi
    BootUi <|.. LvglUi

    class AppManager {
        +load_and_boot(entry, sink) FlashResult
        +refresh_catalog()
        +reboot()
    }
    class AppCatalog {
        +refresh(SdCard&)
        +page(start, count) span
        +count() size_t
        +card_present() bool
    }
    class PicoBootConfig {
        +load(SdCard&) bool
        +save(SdCard&) bool
        +last_run_binary string
        +auto_boot_timeout_s uint32
    }
    class FlashWriter {
        +check_capacity(size, partition_size)$ FlashResult
        +compare_4k(base, image)$ bool
        +erase_and_program(base, image, sink)$
    }
    class VtorJump {
        +relocate_vtor_and_jump(app_base)$
    }
    class FastBoot {
        +consume(tag&)$ bool
        +reboot_into_app()$
        +reboot_into_bootloader()$
    }
    class InterruptGuard {
        RAII interrupt disable/restore
    }

    AppManager o-- AppCatalog
    AppManager o-- PicoBootConfig
    AppManager ..> FlashWriter
    AppManager ..> FastBoot
    FlashWriter ..> InterruptGuard
    VtorJump ..> InterruptGuard
    FastBoot ..> InterruptGuard
```

## Fast-boot sequence

```mermaid
sequenceDiagram
    participant HW as Power-on/reset
    participant Main as main()
    participant FB as FastBoot
    participant RB as pico_toolset::reset_buttons
    participant VJ as VtorJump

    HW->>Main: reset vector fires
    Main->>FB: consume(tag)
    FB->>RB: consume_pending_watchdog_tag()
    alt cold boot / tag == ReenterBootloader
        RB-->>FB: false, or true+ReenterBootloader
        FB-->>Main: full init required
        Main->>Main: stdio/CDC, SD, USB composite, UI, auto-boot countdown
    else tag == BootApp
        RB-->>FB: true, BootApp
        FB-->>Main: fast path
        Main->>VJ: relocate_vtor_and_jump(kAppFlashBase)
        VJ->>VJ: InterruptGuard, VTOR = base+offset, MSP=app SP
        VJ-->>HW: BX app reset handler (never returns)
    end
```

## Known limitation

RP2040-vs-RP2350 architecture compatibility of a loaded `.bin` is **not**
verified (explicit scope decision) — only file-size-vs-partition-size is
checked. A raw, metadata-free `.bin` carries no chip-family marker, and
adding one would require a header/trailer format or a filename convention,
both rejected in favor of pure `.bin` passthrough. Mitigation: a given
physical unit's bootloader is itself built for one specific chip, so in
normal operation a user only copies binaries built for that chip onto that
unit's card.

## Flash partition offset

`kAppFlashBase = 0x10080000` (512KB reserved for the bootloader, `0x10000000`
XIP base + `0x80000`). This is generously sized for the *largest* PicoBoot
variant (`picoboot_lvgl_dvi`), not the smallest, because every target app is
compiled once against this fixed origin regardless of which bootloader
variant flashed it. Measured so far: `picoboot_serial` uses ~56KB of the
512KB reserve (Phase 1 skeleton, before SD/USB/LVGL are added) — comfortable
margin. Revisit at Phase 7 once a real `picoboot_lvgl_dvi` image exists.
Single source of truth: [`cmake/picoboot_flash_layout.cmake`](../cmake/picoboot_flash_layout.cmake),
consumed by [`boot_core/include/picoboot/flash_layout.h.in`](../boot_core/include/picoboot/flash_layout.h.in)
and every testapp's linker override.

### App vector table offset (chip-dependent, verified on RP2350)

Apps are built with pico-sdk's standard `pico_standard_link` flow, just with
`FLASH`'s `ORIGIN` overridden to `kAppFlashBase`. On **RP2350**, this was
verified on a real built `testapps/app_blink.bin`: the vector table (a
plausible RAM stack pointer followed by a matching reset handler address)
sits at **offset 0** from the app's flash base — pico-sdk does not force-link
a `.boot2` stub for this chip (its `.boot2` section is optional and
discarded when unreferenced, unlike RP2040's, which is always exactly 256
bytes and force-linked for any flash build). Since an app loaded via
PicoBoot's VTOR relocation is never cold-booted by the on-chip ROM (only
warm-jumped into from the already-running bootloader), boot2 -- present or
not -- is unused either way; the only thing that matters is matching wherever
the linker actually placed the vector table. See
[`boot_core/src/vtor_jump.cpp`](../boot_core/src/vtor_jump.cpp)'s
`kVectorTableOffset`. **The RP2040 branch (offset `0x100`) is not yet
hardware-verified** — re-check the same way (inspect a real built `.bin`)
during Phase 8's RP2040 bring-up before relying on it.

## Toolset reuse

Reused as-is from Pico-Toolset: `pico_toolset_sdcard`, `pico_toolset_reset_buttons`,
`pico_toolset_fault_handler`, the SPI display drivers (`ili9486`/`st7789`/`st7796`/`xpt2046`)
and `dvi_hdmi`. Net-new, proposed as Pico-Toolset contributions once
hardware-proven: `pico_toolset_usb_composite` (Phase 4) and
`pico_toolset_lvgl_display` (Phase 6/7). Net-new, PicoBoot-only: everything
under `boot_core/`, `config/`, `storage/`, `app_manager/`, `ui/` (safety-critical
or bootloader-specific business logic that doesn't belong in a shared driver
library). See the repo's phased delivery plan for the full 9-phase breakdown.
