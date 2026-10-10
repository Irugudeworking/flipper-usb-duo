# Hardware verification

Tested on 2026-10-10 with a Flipper Zero running Momentum `mntm-dev` commit `25a10b19` (hardware target 7, firmware API 87.1), connected to a Linux PC. The app was compiled with the `mntm-012` SDK and passed uFBT's `APPCHK` for API 87.1.

The normal setup menu and file browsers were exercised before connecting USB. Selecting a script returned to setup without the earlier crash. A layout was selected and cleared back to US. Selecting **Connect USB** enumerated one mass-storage interface and one HID keyboard interface. Linux mounted the supplied FAT16 image and detected both keyboard and mouse input devices.

During the menu-driven diagnostic run, the host verified the 64 KiB `TEST.BIN` checksum 86 times, completed 86 synced writes and readbacks of `VERIFY.TXT`, and received exactly three F24 presses plus three releases from a DuckyScript. After the final UI and USB-serial changes, the installed release binary repeated the concurrent USB test with 109 reads, 109 synced writes, and the same three F24 press/release pairs. The diagnostic script was deliberately harmless and started 15 seconds after connection. The drive unmounted cleanly and the Flipper returned to normal USB serial mode.

The original crash came from building against the stock firmware SDK: Momentum's file-browser options structure has an extra field. The app now builds against Momentum's SDK, passes a compile-time structure-size check, and uses separate path strings for the browser's current location and selected file.

The prebuilt binary has not been tested on other firmware versions or operating systems. USB enumeration and filesystem behavior may vary by host.
