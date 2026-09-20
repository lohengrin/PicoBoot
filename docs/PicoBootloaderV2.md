# PicoBoot

Project is to create a rp2350/rp2040 bootloader.

# Specifications

* bootloader will load binaries from uSD card and execute them as they were flashed normally
* bootloader will stay available at next reboot
* bootloader will expose the uSD content to a host PC using a Composite USB device (simultaneous Mass Storage and Serial)
* bootloader will have multiple UI options (compilation options driven):
* Serial over standard USB tty
  * LVGL UI
  * LCD screen (any supported from Pico-Toolset)
  * DVI screen

# Detailed specifications

## Binary loading & Target Compilation

* **Required Target Changes:** Target applications cannot use the default Pico SDK memory layout, as that would overwrite the bootloader. Applications must be built with a modified linker script that offsets the flash execution address (e.g., `ORIGIN = 0x10040000`).
* **File Format:** Use raw **`.bin`** files. Because the target application handles its own offset compilation, `.bin` files provide a pure, metadata-free payload that the bootloader can rapidly stream directly to the target flash partition.
* Check for available flash size and architecture compatibility (RP2040 vs RP2350) before loading (otherwise error message).
* Binaries are loaded to the dedicated application flash partition.
* Before flashing compare new program with loaded one (using 4KB memcmp to fit flash block size). If identical, just boot, no flash

## Execution & Hardware Teardown

* To guarantee the loaded software receives a pristine hardware state (default clocks, cores, GPIO state, and clean RAM), the bootloader will utilize a Watchdog Soft Reboot.
* Just before executing the loaded software, the bootloader writes a magic flag to a Watchdog Scratch Register (which survives soft reboots) and triggers a watchdog reset.
* **Fast Boot Path:** Upon reboot, the very first instructions of the bootloader check the scratch register. If the flag is present, it clears the flag, relocates the VTOR to the application partition, and jumps directly to the application *before* initializing any UI, USB, or SD card peripherals.

## USB Composite Mode (Storage & Serial)

* The USB connection acts as a Composite Device exposing both CDC (Serial) and MSC (Mass Storage) simultaneously.
* The uSD Card is always exposed to the host as a standard USB mass storage drive.
* Users can drag-and-drop binaries directly to the uSD card while the bootloader UI remains fully active.

## Serial UI

* Display a list of available compatible binaries with numbers starting at 1
* If the list is too long (more than 20 lines) do paging (with display like "showing 21-40 over 110")
* Prompt user for possible options
  * enter a number to load corresponding binary
  * page down/up
  * refresh list (to scan for newly added files via USB storage)
  * reboot with "reboot"
  * possible actions is printed eachtime just before prompt
* ASCII progress bar while loading from uSD to flash

## LVGL UI

* Screen choice at compilation time (LCD-<controller> or HDMI) or none
* Propose a listview with compatible files
* Title "PicoBoot" "by Lohengrin" (smaller)
* Selection can be done using any controllers [keyboard, touchscreen (if supported by screen), joypad, mouse]
* Add buttons (trying to place them to avoid reducing the listview vertical size)
  * Refresh List (to detect new files dropped via USB)
  * Reboot
* Graphical progress bar while loading from uSD to flash

# Additional instructions

* Must use **Pico-Toolset** (local clone `~/Dev/Pico-Toolset`) for drivers and reusable code (can propose contributions to this project to improve)
* Two existing projects having such UI, uSD card (reuse code / technical solutions as possible, if really same code, propose to promote to Pico-Toolset project)
  * TOM6809:
  * PicoDoom:
* Keep booloader size small as possible to keep space for application
* If no uSD card display no "µSD card" instead of list view, keep reboot and refreash button available, refresh will retry to mount the card
* A config file is stored on the sdcard "picoboot.cfg"
  * store last run binary
  * auto boot timeout (s) : 0 = none, default = 30s

# Architecture

* Propose a clean software architecture with clear separation between functions.
* Make an UML diagram of the target architecture
* Provides some unit test programs to be put on the uSD card during devellopement with needed target modification for testing.
* Take care of disabling interrupts when in critical area (flashing, Vector Table Offset Register, Stack pointer ...)
* Use **Pico-Toolset** as git submodule from `https://github.com/lohengrin/Pico-Toolset.git` (if doing modification, work in submodule folder, do not modify other local clone)
