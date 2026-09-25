# Lilka v2 ScummVM guest — SCUMM and Kyra

The single `feature/lilka-scummvm` branch builds separate `scumm.bin` and
`kyra.bin` guest images. This document retains the SCUMM bring-up and device
notes; see `README-LILKA-KYRA.md` for Kyra-specific data and controls.

Physical A (GPIO5) is the primary left click and B (GPIO6) is the secondary
right click when using the example manifest's A/B actions.

These are experimental ESP-IDF 5.3.2 builds with one engine per image. They are
separate from Keira; Keira's `.scummvm` manager lives on its own
`feature/scummvm-manager` branch. A matching manager build can pass a
CRC-protected manifest path through RTC memory. This guest validates the
manifest and starts the selected game with `--auto-detect`, bypassing
the stock launcher. Opening the
raw `.bin` without a manifest continues to show the stock launcher.

## Build and image

From the repository root, run `./compile-all-lilka.sh` to build both engines,
or `./compile-lilka.sh scumm` / `./compile-lilka.sh kyra` for one. It loads ESP-IDF 5.3.2 from
the sibling `../esp/esp-idf` directory (or from `IDF_PATH` if set), requires
`cmake` and `ninja` on `PATH`, builds the project, and checks the image size.
It does not flash the device. Alternatively, from this directory with
ESP-IDF 5.3.2 already active, run `idf.py -D LILKA_ENGINE=scumm build`. The
batch outputs are `build/lilka-engines/scumm.bin`, `kyra.bin`, and `kyra.dat`.
Do not flash the T-Deck merged image, bootloader,
partition table, or OTA data onto Lilka. The binary must
remain at or below `0x640000` bytes to fit Keira's `app1` OTA slot. This
project’s `partitions.csv` mirrors Keira v2's `default_16MB.csv` for size
checking; it is not a request to replace the device's partition table.

### Building on Windows (WSL2)

This fork's ScummVM component invokes POSIX `export`, `./configure`, and
`make` during the build. A native Windows PowerShell or Command Prompt build
is therefore not supported. Use Ubuntu in WSL2. A WSL-native checkout is
fastest; `/mnt/d/Software` can be used as a project root, though this path has
not yet been device/build tested and Windows-mounted drives can be slower.
Clone from within WSL so build scripts
retain Unix line endings. The same `compile-all-lilka.sh` works there, including
its size check; no Windows-specific firmware image is needed.
After a successful build, the script also copies `scumm.bin`, `kyra.bin`, and
`kyra.dat` to `/mnt/d/Software/scummvm-lilka` when that directory exists.

In an **administrator PowerShell** window, install WSL if it is not already
available, then restart Windows if prompted:

```powershell
wsl --install -d Ubuntu
```

Open the Ubuntu terminal and install the build tools, ESP-IDF 5.3.2, and this
feature branch:

```bash
sudo apt update
sudo apt install -y git wget flex bison gperf python3 python3-pip python3-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 build-essential
mkdir -p /mnt/d/Software/esp
git clone --recursive --branch v5.3.2 https://github.com/espressif/esp-idf.git /mnt/d/Software/esp/esp-idf
cd /mnt/d/Software/esp/esp-idf
./install.sh esp32s3
git clone --branch feature/lilka-scummvm https://github.com/fideleka/scummvm-lilka.git /mnt/d/Software/scummvm-lilka
cd /mnt/d/Software/scummvm-lilka
./compile-all-lilka.sh
```

If `scummvm-lilka` is already checked out at `/mnt/d/Software`, skip its clone
command, then run `git fetch origin`, `git switch feature/lilka-scummvm`,
and `git pull --ff-only` from that checkout. Do not reuse a Windows ESP-IDF
Python/toolchain installation inside WSL; `./install.sh esp32s3` must run in
Ubuntu. For subsequent builds, run `cd /mnt/d/Software/scummvm-lilka`, update
the branch with `git pull --ff-only` if desired, then run
`./compile-all-lilka.sh` again.
If ESP-IDF is installed elsewhere within WSL, set `IDF_PATH` to that path
before running the script. The outputs are `build/lilka-engines/scumm.bin` and
`kyra.bin` inside the checkout. Copy those raw images to the SD card under
`scummvm/engines/`.
Do not run `idf.py flash` on Lilka.

For a raw-image hardware check, copy the raw image to the SD card as
`/sd/scummvm/engines/scumm.bin`, then open that `.bin` from Keira's File
Manager. Keira's existing multiboot writer installs it into `app1` and starts
it. It should display the ScummVM launcher. Hold Select + Start for 1.5
seconds to return to Keira. Check that Keira, Start-alone input, and PC COM
all recover. The first Lilka test reached the ScummVM launcher but found the
display upside down. The panel orientation has since been corrected; the new
image still needs a device retest. The stock launcher remains too small for
comfortable use on Lilka and is only a temporary bring-up interface until
Keira's direct-launch manager and the guest handoff are implemented.

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

The first direct Maniac Mansion launch stalled on the ScummVM startup logo.
The exit chord now has an independent FreeRTOS poll task so a blocked
ScummVM main loop does not prevent returning to Keira. A CPU panic can still
stop the scheduler: this build uses the ESP-IDF GDB panic stub, so capture the
serial panic/backtrace to distinguish that case from a main-loop hang.
The captured panic was an ESP-IDF SPI HAL assertion during the block-cache
worker's SD preread. LCD and SD share SPI2; this candidate serializes complete
LCD DMA batches with physical SD reads/writes using one bus mutex. It builds,
and the first Lilka retest reached Maniac Mansion gameplay. Physical C opened
the save/load menu, saving/loading worked in-game, and Select + Start returned
to Keira. Save persistence across a restart, normal game-quit return, and
longer display/audio testing remain open.

For manager launches, the `.scummvm` manifest can remap A/B/C/D/Start/Select
to `leftClick`, `rightClick`, `enter`, `escape`, `space`, `f5`, `f7`,
`virtualKeyboard`, or `none`. It can also tune D-pad pointer speed with
`pointer.slowStep`, `pointer.fastStep`, and `pointer.accelerationMs`. The
Select + Start return chord is always enabled. The matching Keira branch
contains Maniac Mansion and Monkey Island 1 example manifests; no game data is
bundled. In the Maniac example, C sends F5 for Save/Load and D sends Escape.
Select requests the virtual keyboard, which requires a separate support pack
on SD and was unavailable in the first test.
