# Lilka v2 SCUMM guest — first hardware candidate

This is an experimental ESP-IDF 5.3.2 build of the `scumm` engine only. It is
separate from Keira; Keira's `.scummvm` manager lives on its own
`feature/scummvm-manager` branch. Direct manifest launch is **not implemented
in this candidate**. The inherited ScummVM launcher is used for the first
boot/display/input/return proof.

## Build and image

From the repository root, run `./compile-lilka.sh`. It loads ESP-IDF 5.3.2 from
the sibling `../esp/esp-idf` directory (or from `IDF_PATH` if set), requires
`cmake` and `ninja` on `PATH`, builds the project, and checks the image size.
It does not flash the device. Alternatively, from this directory with
ESP-IDF 5.3.2 already active, run `idf.py build`. The **raw application image**
is `build/scummvm.bin`. Do not flash the T-Deck merged image, bootloader,
partition table, or OTA data onto Lilka. The binary must
remain at or below `0x640000` bytes to fit Keira's `app1` OTA slot. This
project's `partitions.csv` mirrors Keira v2's `default_16MB.csv` for size
checking; it is not a request to replace the device's partition table.

For the first hardware check, copy the raw image to the SD card as
`/sd/scummvm/engines/scumm.bin`, then open that `.bin` from Keira's File
Manager. Keira's existing multiboot writer installs it into `app1` and starts
it. It should display the ScummVM launcher. Hold Select + Start for 1.5
seconds to return to Keira. Check that Keira, Start-alone input, and PC COM
all recover. These checks have not yet been performed on a Lilka device.

## SD card

All writable data lives on physical SD:

- `/sd/scummvm/scummvm.ini`
- `/sd/scummvm/saves/`
- `/sd/scummvm/data/themes/` (for example `scummmodern.zip`)
- `/sd/scummvm/data/engine-data/` (for example `vkeybd_small.zip`)
- `/sd/games/scummvm/` (user-supplied game folders)

No game data is included in the image. The old T-Deck `flash_data` directory
is still in the fork for upstream reference but is not built into Lilka's
firmware and no LittleFS partition is used.

## Provisional controls

- D-pad: pointer, accelerating after 0.5 seconds
- A/B: left/right click
- C/D: F5/F7
- Start: Enter
- Select: virtual keyboard
- Hold Select + Start: reboot toward Keira

The layout and display orientation need device validation. Do not regard this
as a game-ready release until the manifest handoff, save/load cycle, and
rollback are proven on hardware.
