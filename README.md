# USB Duo for Flipper Zero

USB Duo runs a BadUSB DuckyScript and presents a USB storage drive **at the same time**. The drive is a FAT disk-image file kept on the Flipper's SD card. The Flipper reads the script from its SD card; the computer sees a keyboard/mouse and the selected image as a removable drive.

This build targets **Momentum firmware, Flipper hardware target 7, API 87.1**. It was compiled with Momentum's `mntm-012` SDK and tested on a Flipper running `mntm-dev` (`25a10b19`, API 87.1). A matching API number on a different firmware is not enough to guarantee compatibility: Momentum extends the file-browser API used by this app. Do not install this `.fap` on stock firmware without rebuilding and adapting it for that firmware.

## Download and install

Download these files from this repository:

| File | Copy to the Flipper SD card |
| --- | --- |
| [`dist/usb_duo.fap`](dist/usb_duo.fap) | `/ext/apps/USB/usb_duo.fap` |
| [`examples/DUO.img`](examples/DUO.img) | `/ext/apps_data/usb_duo/DUO.img` |
| [`examples/usb_duo_demo.txt`](examples/usb_duo_demo.txt) | `/ext/badusb/usb_duo_demo.txt` (optional) |

The supplied `DUO.img` is an 8 MiB FAT16 drive with a README and test file. It contains no payload or automatic script. You may replace it with another FAT-formatted raw `.img` file.

1. Connect the Flipper to your computer in its ordinary USB mode. Close any running USB app.
2. Open the [qFlipper file manager](https://docs.flipper.net/zero/qflipper), or use an SD-card reader. Create the destination folders above if they are missing, then copy the files. If the app file already exists, replace it.
3. On the Flipper, open **Apps → USB → USB Duo**. If it does not appear, refresh the app list or restart the Flipper.
4. Keep the Flipper connected by USB. USB Duo only changes its USB mode when you choose **Connect USB**.

The app is a standalone `.fap`; you do **not** have to replace or flash the firmware. Keep your firmware and this app on compatible API versions.

## Use

The setup screen has four rows:

- **Disk**: choose an existing `.img` file on the SD card. The bundled image is `/ext/apps_data/usb_duo/DUO.img`. This is the drive the computer will mount.
- **Script**: choose a BadUSB `.txt` script, normally under `/ext/badusb`.
- **Layout**: choose a `.kl` keyboard layout matching the target computer, or press **Left** on this row for the built-in US layout.
- **Connect USB**: activate the combined storage and HID device. The app remembers your selections for next time.

On the active screen, **OK** starts or stops the script, **Right** pauses or resumes it, and **Left/Back** returns to setup. The display shows script state, progress, disk read/write counts and errors. USB Duo asks you to eject the drive before disconnecting. Eject/unmount it on the computer first to avoid corrupting the image.

For a harmless keyboard check, open a blank text editor on the computer, select `usb_duo_demo.txt`, choose **Connect USB**, then press **OK**. It waits two seconds and types one sentence. The normal app never runs the script merely because you selected it or connected USB.

## Where files go

Only the **selected image** is exposed as USB storage. The rest of the Flipper SD card is not mounted on the computer. Copy files into the mounted drive from your computer, or prepare the image before copying it to the Flipper. Scripts can type commands that refer to those files while the drive remains mounted. The host operating system chooses the drive letter or mount path, so a script must use the right path for that computer.

```text
Flipper SD card
├── /ext/badusb/your_script.txt   → DuckyScript read by Flipper
└── /ext/apps_data/usb_duo/DUO.img → FAT drive exported over USB
                                      ↓
Computer sees: USB keyboard/mouse + mounted removable drive
```

Do not edit the `.img` through another Flipper app while USB Duo is using it. The computer can read and write the mounted drive while a DuckyScript runs. USB serial/qFlipper disconnects during this USB mode and returns after you leave it.

## DuckyScript behavior

USB Duo uses Flipper's BadUSB script engine. Ordinary DuckyScript `.txt` commands such as `STRING`, `ENTER`, `DELAY`, key combinations, mouse actions, repeats and hold/release remain available. Scripts are not copied into the drive image. Bluetooth BadUSB is outside this app. A script's `ID` line is parsed, but this app keeps a fixed composite USB identity, so that line cannot change its USB VID/PID.

## Other drive images

Select any existing FAT16/FAT32 raw image that is a multiple of 512 bytes and below 4 GiB. For example, on Linux:

```bash
truncate -s 16M MYDRIVE.img
mkfs.fat -F 16 -s 1 -n MYDRIVE MYDRIVE.img
```

Copy `MYDRIVE.img` to a location on the Flipper SD card, choose it from the **Disk** row, and copy files into the drive after the computer mounts it. Do not select an unformatted image: the computer will not see a usable filesystem. The app does not create or format images for you.

## Build from source

Use the [Momentum SDK release `mntm-012`](https://github.com/Next-Flip/Momentum-Firmware/releases/tag/mntm-012) and [uFBT](https://github.com/flipperdevices/flipperzero-ufbt). From the repository root:

```bash
python3 -m venv .venv
. .venv/bin/activate
pip install ufbt
export UFBT_HOME="$PWD/.ufbt"
ufbt update --hw-target f7 --url https://github.com/Next-Flip/Momentum-Firmware/releases/download/mntm-012/flipper-z-f7-sdk-mntm-012.zip
ufbt
```

The output is `dist/usb_duo.fap`. The included prebuilt file is for the tested Momentum API 87.1 firmware. If your firmware has a different API, rebuild against its matching SDK and review the USB and file-browser interfaces before installing.

## Test and limits

The menu path was tested on hardware: selecting a script returned to setup, a keyboard layout was selected and reset, and **Connect USB** exposed both interfaces. On Linux, a menu-driven run passed **86 read checks and 86 synced write checks**, and the final installed binary passed **109 read checks and 109 synced write checks** while three F24 keypress/release pairs arrived through HID. The 8 MiB image remained mountable and the Flipper returned to serial mode after the diagnostic session. See [`docs/verification.md`](docs/verification.md) for the exact setup and checks. Windows, macOS and BIOS boot-keyboard behavior have not been hardware-tested.

USB Duo requires an SD card. The chosen image must be writable and have a valid sector-aligned size. Eject the drive before leaving the active screen. If a script references files by drive letter, adjust that letter for the target computer.

## License and upstream work

GPL-3.0; see [`LICENSE`](LICENSE). Source derived from:

- [Flipper Zero firmware](https://github.com/flipperdevices/flipperzero-firmware), commit `76bddb682f907928afab3bbd9e015bc4e3e4ee6e`: BadUSB parser and HID reports/transport.
- [Flipper Zero official apps](https://github.com/flipperdevices/flipperzero-good-faps), commit `55328b486971e49078a955a7c086e4463fa6843b`: Mass Storage SCSI and USB bulk transport.

USB Duo adds the composite descriptors and routing, separate HID endpoints, app UI, disk-image selection, persistent choices, safe disconnect prompt, and concurrent I/O support.
