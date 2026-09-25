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
project’s `partitions.csv` mirrors Keira v2's `default_16MB.csv` for size
checking; it is not a request to replace the device's partition table.

### Building on Windows (WSL2)

This fork's ScummVM component invokes POSIX `export`, `./configure`, and
`make` during the build. A native Windows PowerShell or Command Prompt build
is therefore not supported. Use Ubuntu in WSL2. A WSL-native checkout is
fastest; `/mnt/d/Software` can be used as a project root, though this path has
not yet been device/build tested and Windows-mounted drives can be slower.
Clone from within WSL so build scripts
retain Unix line endings. The same `compile-lilka.sh` works there, including
its size check; no Windows-specific firmware image is needed.
After a successful build, the script also replaces
`/mnt/d/Software/scummvm-lilka/scummvm.bin` when that directory exists. The
original image remains in the WSL checkout at
`backends/platform/esp32/build/scummvm.bin`.

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
git clone --branch feature/lilka-scumm-only https://github.com/fideleka/scummvm-lilka.git /mnt/d/Software/scummvm-lilka
cd /mnt/d/Software/scummvm-lilka
./compile-lilka.sh
```

If `scummvm-lilka` is already checked out at `/mnt/d/Software`, skip its clone
command, then run `git fetch origin`, `git switch feature/lilka-scumm-only`,
and `git pull --ff-only` from that checkout. Do not reuse a Windows ESP-IDF
Python/toolchain installation inside WSL; `./install.sh esp32s3` must run in
Ubuntu. For subsequent builds, run `cd /mnt/d/Software/scummvm-lilka`, update
the branch with `git pull --ff-only` if desired, then run
`./compile-lilka.sh` again.
If ESP-IDF is installed elsewhere within WSL, set `IDF_PATH` to that path
before running the script. The output is
`backends/platform/esp32/build/scummvm.bin` inside the checkout, visible in
Windows Explorer at `D:\Software\scummvm-lilka\backends\platform\esp32\build\scummvm.bin`.
Copy that raw file to the SD card as `scummvm/engines/scumm.bin`.
Do not run `idf.py flash` on Lilka.

For a hardware check, copy the raw image to the SD card as
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
