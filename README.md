# PicoBoot

A bootloader for the RP2040 and RP2350 that runs applications straight from a microSD card.

Copy a `.bin` onto the card (or drop it over USB), pick it from a menu, and PicoBoot flashes it into a
reserved partition and starts it — as if it had been flashed normally. It stays resident, so the next reset
brings the menu back.

## Features

- **Loads raw `.bin` files from a uSD card** into an application partition and boots them.
- **Streams the image** in 16 KiB blocks (RAM use is one block, so large apps fit the RP2040's 264 KB),
  compares each 4 KiB sector with what is already in flash and rewrites **only the sectors that differ** —
  re-loading an identical image writes nothing. Every block is read back and verified; a failed image is
  never booted.
- **Inspects every image before touching flash**, from the `.bin` itself: which **chip family** it was built
  for (RP2040 boot stage checksum / RP2350 image definition), whether it is an Arm image, and **where it was
  linked** (from the reset vector). Anything it cannot run is refused with a clear message on the screen
  and on serial (wrong chip, RISC-V, not an app image, linked for an unsupported address, too large).
- **Normal builds run unmodified on RP2350:** an application linked normally (at `0x10000000`) is detected
  and started through the flash controller's address translation. RP2040 apps are linked for the partition
  (see below).
- **USB composite device:** the SD card appears as a **mass-storage drive** (drag and drop apps while the
  UI stays up), plus a **CDC serial** port and the **picotool reset interface** — `picotool load -f` /
  `reboot -f -u` work without touching BOOTSEL.
- **Several UIs**, chosen per target: a serial text menu (always available alongside the others), and an LVGL
  graphical UI on an LCD or on HDMI/DVI.
- **Auto-boot** of the last-run app after a configurable timeout; `picoboot.cfg` on the card.
- **Pristine start for the app:** apps are launched after a watchdog reset, before any bootloader
  peripheral is initialised, so they start in the normal power-on state.

## Supported boards

Each board is a separate build directory (`-DPICOBOOT_BOARD=...`). Everything below has been run on real
hardware, including normal builds on the RP2350 (address translation) and the refusal cases.

| Board | `PICOBOOT_BOARD` | Chip | UI targets | Input |
|---|---|---|---|---|
| Waveshare RP2350-PiZero | `waveshare_pizero` (default) | RP2350 | serial; **LCD** (external 3.5" **ILI9486** or **ST7796U** panel + touch); **HDMI** | touch (LCD); USB keyboard / mouse / gamepad on the PIO-USB port (HDMI) |
| Elecrow CrowPanel PICO HMI 2.8" | `crowpanel_pico_hmi_28` | RP2040 | serial; **LVGL on the built-in ST7789** | touch |
| "Pico DV" carrier + Pico W | `pico_dv` | RP2040 | serial; **LVGL on HDMI** | 3 buttons |

Flash footprint (bootloader reserve is 512 KiB):

| Target | Flash | RAM |
|---|---|---|
| `picoboot_serial` | ~110 KiB | ~41 KiB |
| `picoboot_lvgl_lcd` / `_st7796` (Waveshare) | ~352 KiB | ~175 KiB of 520 KiB |
| `picoboot_lvgl_dvi` (Waveshare) | ~378 KiB | ~321 KiB of 520 KiB |
| `picoboot_lvgl_crowpanel` | ~365 KiB | ~117 KiB of 264 KiB |
| `picoboot_lvgl_dvi` (Pico DV) | ~366 KiB | ~186 KiB of 264 KiB |

## Building

**Requirements:** the [Pico SDK](https://github.com/raspberrypi/pico-sdk) 2.x (tested with 2.3.1),
`arm-none-eabi-gcc`, CMake ≥ 3.20, Python 3, and network access on first configure (LVGL 9.2.2, `pico_fatfs`
and, for the Waveshare board, Pico-PIO-USB are fetched by CMake).

```bash
git clone --recurse-submodules <this repo> PicoBoot     # Pico-Toolset is a submodule
cd PicoBoot
export PICO_SDK_PATH=/path/to/pico-sdk

# Waveshare RP2350-PiZero (default) -> serial, LCD and HDMI targets
cmake -S . -B build && cmake --build build -j

# CrowPanel PICO HMI 2.8" (RP2040)
cmake -S . -B build-crowpanel -DPICOBOOT_BOARD=crowpanel_pico_hmi_28 && cmake --build build-crowpanel -j

# Pico DV + Pico W (RP2040)
cmake -S . -B build-picodv -DPICOBOOT_BOARD=pico_dv && cmake --build build-picodv -j
```

Outputs are `build*/targets/<target>/<target>.uf2`: `picoboot_serial`, `picoboot_lvgl_lcd` (ILI9486 panel),
`picoboot_lvgl_lcd_st7796` (ST7796U panel), `picoboot_lvgl_dvi` (Waveshare), `picoboot_lvgl_crowpanel`, `picoboot_lvgl_dvi` (Pico DV). The default build
type is `MinSizeRel`; each bootloader is linked into a flash region limited to its 512 KiB reserve, so
outgrowing it fails at link time instead of overwriting the application partition.

## Installing the bootloader

First time: hold **BOOTSEL**, plug the board in, then

```bash
picotool load -f build/targets/picoboot_lvgl_lcd/picoboot_lvgl_lcd.uf2
picotool reboot
```

(or drag the `.uf2` onto the `RP2350`/`RPI-RP2` drive). After that, PicoBoot exposes the picotool reset
interface, so updating it needs no button:

```bash
picotool load -f <new picoboot>.uf2 && picotool reboot
```

Other useful commands: `picotool reboot -f -u` (into BOOTSEL), `picotool reboot -f` (back into PicoBoot).
The device uses VID/PID `2e8a:000a`, which the stock picotool udev rules already cover.

> Load a `.uf2` built for the default `0x10000000` layout with picotool and you **overwrite the bootloader**.
> Applications go on the SD card as `.bin` files (below).

## Using it

1. Put `.bin` files in the **root** of a FAT-formatted uSD card (or connect the board and copy them over the
   USB drive).
2. Reset the board. The UI lists the files. If `auto_boot_timeout` is set and a last-run app exists, a
   countdown starts (any key or touch cancels it). Auto-boot is skipped when an app deliberately returns to
   the bootloader.
3. Pick a file to load. A progress bar is shown; the app starts when flashing finishes.

**Refresh after copying:** the bootloader and the USB host both read the card but do not stay in sync live.
After adding or changing files over USB, use *Refresh* (or `r`) so the list — and the bootloader's view of
the card — is updated. While an app is being loaded the USB drive is **read-only** (a host write is refused as write-protected), so a
file cannot change under the loader.

### Serial menu

Open the CDC port (e.g. `screen /dev/ttyACM0 115200`). It runs alongside the LVGL UI on every target.

| Input | Action |
|---|---|
| a number | load that file |
| `n` / `p` | next / previous page (20 entries per page, `showing 21-40 over 110`) |
| `r` | refresh: remount the card and rescan |
| `info` | storage diagnostics: card state and FatFs result, capacity, files listed, USB write lock, stack and heap headroom |
| `reboot` | reboot into the bootloader |
| `bootsel` | reboot into the ROM's USB BOOTSEL mode |
| `buttons` | (Pico DV only) diagnostic: report which free pin changes when a button is pressed |

Failures print a line naming the step (`load: verify failed at image offset ...`, `load: fopen(...)`, ...).

### LVGL UI

Title "PicoBoot" / "by Lohengrin", a list of the `.bin` files, **Refresh** and **Reboot** buttons (kept in the
header so the list has the vertical space), a status row (auto-boot countdown, errors) and a graphical
progress bar. Input by board: touch (LCD boards), USB keyboard / mouse / gamepad (Waveshare HDMI; arrows or
Tab move, Enter selects), or the three buttons on the Pico DV (**A** down, **B** up, **C** select).

### `picoboot.cfg`

A text file in the card's root, created on first load:

```
last_run=myapp.bin
auto_boot_timeout=30
```

`auto_boot_timeout` is in seconds; `0` disables auto-boot (default 30).

## Making an application that PicoBoot can run

PicoBoot reads the `.bin` and decides how to start it, so there are two kinds of application:

| Application | Build | RP2350 | RP2040 |
|---|---|---|---|
| **Normal build** (linked at `0x10000000`, as any Pico SDK project) | nothing special | runs (started through flash address translation) | **refused** — the RP2040 cannot map flash to another address |
| **Partition build** (linked at `0x10080000`) | link for the partition, below | runs | runs |

Either way the file to put on the card is the raw **`.bin`** (not the `.uf2`) — the file objcopy produces from the
ELF. Only apps built for the chip the bootloader runs on are accepted, and only Arm images (not RP2350 RISC-V).

**Partition build** (works on both chips). The partition starts at `0x10080000` (512 KiB after the flash base;
defined once in `cmake/picoboot_flash_layout.cmake`). Use the helper:

```cmake
include(/path/to/PicoBoot/cmake/picoboot_app_linker.cmake)
# second argument: the board's total flash size in bytes
picoboot_set_app_flash_region(my_app 16777216)
pico_add_extra_outputs(my_app)     # produces my_app.bin
```

**Caution for normal builds on RP2350.** The flash is remapped only for *reading* code. Flash **programming**
APIs (`flash_range_erase` / `flash_range_program`) use physical flash offsets and are not translated, so a
normal build that stores data in flash at a low offset (e.g. "just after my program") would write over the
bootloader. Apps that store data at the end of flash are fine. The mapping is undone by the next reset.

To return to the bootloader from the app, reboot with the shared tag (this is what `testapps/app_reboot_to_bootloader`
does; the value lives in `boot_core/include/picoboot/boot_tags.h`):

```cpp
#include "pico_toolset/reset_buttons.h"
pico_toolset::watchdog_reboot_with_tag(2 /* BootTag::kReenterBootloader */);   // never returns
```

Anything else is a normal reset, so the bootloader menu comes back.

### Test applications

`testapps/` holds small apps for bring-up, each a standalone CMake project selectable with
`-DPICOBOOT_TEST_BOARD=waveshare_pizero|crowpanel_pico_hmi_28|pico_dv`:

| App | Expected behaviour |
|---|---|
| `app_blink` | blinks an LED (the Pico W's onboard LED, GPIO19 on the CrowPanel, GPIO15 on the Waveshare board) |
| `app_reboot_to_bootloader` | blinks briefly, then returns to the PicoBoot menu |
| `app_normal_build` | a completely normal build (`0x10000000`): **runs on RP2350** (blinks), must be **refused on RP2040** ("linked for 0x10000000: unsupported on RP2040") |

```bash
cd testapps/app_blink
cmake -S . -B build-pico_dv -DPICOBOOT_TEST_BOARD=pico_dv && cmake --build build-pico_dv
# copy build-pico_dv/app_blink.bin onto the SD card
```

## How it works (short)

- **Flash layout:** `0x10000000`–`0x1007FFFF` (512 KiB) is PicoBoot; the app partition starts at
  `0x10080000`. The reserve is sized for the largest UI variant so applications, all linked once against
  that fixed origin, never need rebuilding when the bootloader changes.
- **Boot path:** to start an app, PicoBoot writes a tag into watchdog scratch registers and resets. On the
  next boot the very first thing `main()` does is check the tag; if it says "boot the app", it relocates
  `VTOR` to the app's vector table, loads its stack pointer and jumps — before any clock, USB, SD or UI
  initialisation. Everything else falls through to the full bootloader.
- **Flashing:** image blocks are compared and rewritten inside `flash_safe_execute()` critical sections; a
  second core running video is parked one block at a time, and progress is reported between blocks.
- **Layers:** `boot_core` (fast-boot, VTOR jump, flash writer — no dependency on SD/USB/UI) ← `app_manager`
  ← `ui` (serial and LVGL backends behind one interface) ← `targets`; `storage`, `config` and `usb` sit
  beside them. See [`docs/architecture.md`](docs/architecture.md) for diagrams, the flash design and the
  per-chip details.

Drivers come from the [Pico-Toolset](https://github.com/lohengrin/Pico-Toolset) submodule
(`third_party/pico-toolset`): SD card, display and touch drivers, DVI/HDMI, the USB composite device
(`usb_composite`), the LVGL bridge (`lvgl_display`) and the watchdog-tag reboot. Changes to shared code are
made in the submodule.

## Repository layout

```
boot_core/     fast-boot tag, VTOR jump, streaming flash writer, image checks
config/        picoboot.cfg
storage/       AppCatalog (list of .bin files)
app_manager/   load-and-boot orchestration
usb/           USB bridge: SD card raw sectors <-> mass-storage
ui/            BootUi interface; serial/ and lvgl/ backends; lv_conf.h
targets/       one directory per firmware image; common/board.h holds per-board presets
testapps/      bring-up applications and the linker template
cmake/         flash layout (single source of truth), app linker helper, board selection
docs/          architecture and design notes
third_party/pico-toolset   git submodule
```

## Limitations and notes

- **Chip family:** an RP2040 image is refused on an RP2350 board and the reverse, and RISC-V images are refused —
  each with a message naming both chips. (Detection uses the RP2040 boot-stage checksum and the RP2350 image
  definition block, which every Pico SDK build contains.)
- **Normal builds need an RP2350.** On an RP2040 the application must be linked for the partition
  (`0x10080000`); a normal build is refused with the reason and the fix.
- **Normal builds and flash writes (RP2350):** see the caution in *Making an application*.
- **Card and drive:** the drive is FAT12/16/32 or exFAT (first partition; a GPT-partitioned card is reported as
  "no FAT/exFAT volume"). The bootloader lists `.bin` files in the **root folder** only, skips hidden/system
  files and `._*` sidecars, and shows at most 512 files. A removed card is noticed by the drive within a second;
  press *Refresh* after re-inserting it. A host "eject" flushes the card and hides the drive until you press
  *Refresh* (or replug).
- **Video stalls briefly while flashing** on the HDMI targets (the video core is parked one 16 KiB block at a
  time).
- The **Pico DV** UI uses an 8-bit (RGB332, dithered) canvas to fit in RAM: whites are slightly yellow.
- The bootloader itself is updated with picotool / BOOTSEL, not from the SD card.
- The CrowPanel touch calibration and the Pico DV button pins (GP14/15/16) are specific to those boards.

## Troubleshooting

| Symptom | Check |
|---|---|
| picotool says "No accessible RP-series devices" | `lsusb -d 2e8a:` — the device must be there and your user must be allowed to open it (picotool's udev rules, PID `000a`); as a test try `sudo` |
| The drive shows the old file list | press *Refresh* / `r` after copying files |
| "uSD card has no FAT/exFAT volume" | the card is unformatted or GPT-partitioned: format it as FAT32 / exFAT with an MBR partition table |
| "uSD card stopped responding" | the card was removed or its contacts failed; re-insert and press *Refresh* |
| Something odd with a big folder or memory | `info` shows the free stack and heap; 512 files are listed at most |
| "linked for 0x10000000: unsupported on RP2040" | a normal build on an RP2040 board: rebuild the app with `picoboot_set_app_flash_region` |
| "built for RP2040, this board is RP2350" (or the reverse) | the app was built for the other chip family |
| "does not fit in the application partition" | the `.bin` is larger than the flash minus the 512 KiB reserve |
| `load: ...` line on the serial port | it names the failing step (`stat`, `fopen`, header read, streaming read, flash critical section, verify) |
| No picture on HDMI | HDMI needs exactly 252 MHz; make sure you flashed the target built for your board |

## License

PicoBoot is released under the [MIT License](LICENSE). The build also pulls in third-party code under
their own licences: the Pico SDK (BSD-3-Clause), LVGL (MIT), FatFs / `pico_fatfs`, Pico-PIO-USB and the
vendored DVI code inside the Pico-Toolset submodule (see that repository for its notices).

## Related

- Full specification: [`PicoBootloaderV2.md`](PicoBootloaderV2.md)
- Architecture and design notes: [`docs/architecture.md`](docs/architecture.md)
