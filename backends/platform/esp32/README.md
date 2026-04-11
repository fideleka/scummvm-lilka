
ScummVM for LilyGo T-Deck (ESP32-S3)
====================================

A port of [ScummVM](https://github.com/scummvm/scummvm) to the
[LilyGo T-Deck v1](https://github.com/Xinyuan-LilyGO/T-Deck): ESP32-S3,
16 MB flash, 8 MB octal PSRAM, 320×240 ST7789 SPI display, MAX98357A I2S
speaker, BlackBerry Q10 I2C keyboard, optical trackball, microSD.

The firmware ships with **The Secret of Monkey Island (EGA Demo)** baked
into a built-in LittleFS partition, so it's playable out of the box without
preparing an SD card.

Flashing a pre-built firmware
-----------------------------

You don't need to build anything — grab the latest pre-built firmware from
the [GitHub Releases](../../releases) page (the file is called
`scummvm-tdeck-full.bin`) and flash it with `esptool`.

```bash
# 1. One-time: install esptool
pip install esptool

# 2. Put the T-Deck in download mode:
#      hold BOOT, tap RESET, release BOOT
#    The screen stays dark — that's expected.

# 3. Find the serial port:
#      macOS:   ls /dev/cu.usbmodem*
#      Linux:   ls /dev/ttyACM*
#      Windows: Device Manager → Ports → COMxx

# 4. Flash:
python -m esptool --chip esp32s3 -p <PORT> -b 921600 \
    write_flash 0x0 scummvm-tdeck-full.bin
```

Replace `<PORT>` with whatever you found in step 3 (e.g.
`/dev/cu.usbmodem1101`, `/dev/ttyACM0`, `COM5`).

Flashing takes about a minute. The T-Deck reboots into ScummVM
automatically; the bundled Monkey Island 1 EGA demo appears in the
launcher list.

There's also a tiny `flash.sh` wrapper in this directory that does the
same thing — just put the `.bin` next to it and run `./flash.sh`.

Controls
--------

- **Trackball** — moves the mouse cursor.
- **Trackball click** — left mouse button.
- **BBQ10 keyboard** — typing in dialogs and game text fields.
- **Speaker key** (top-left extra) → F5 (save menu in LucasArts games).
- **Mic key** → F7 (load menu).
- **Sym / Alt key** → Alt modifier.
- The on-screen cursor is rendered bright yellow so it's visible
  against any game background.

Bundled content
---------------

The 4 MB built-in LittleFS partition contains:

- `/sdcard/scummvm/scummmodern.zip` — GUI theme
- `/sdcard/scummvm/sky.cpt`, `queen.tbl`, `vkeybd_small.zip` — engine data
- `/sdcard/games/monkey1/` — Monkey Island 1 EGA demo (freeware from
  scummvm.org)

The Monkey Island 1 demo is pre-registered in the launcher list — just
select it and click Start.

Adding more games
-----------------

A real microSD card (FAT32, FAT16) is also supported and gets mounted at
`/sd` so it doesn't shadow the bundled LittleFS at `/sdcard`. Drop game
folders anywhere on the card, then in the launcher use **Add Game…** and
browse to `/sd/<your-game-folder>`.

If your card isn't being detected, check that it's FAT-formatted and try
reseating it. SD detection on T-Deck v1 has some hardware variability.

Building from source
--------------------

You only need this if you want to change the firmware itself.

You'll need ESP-IDF 5.3 or newer. With it active:

```bash
cd backends/platform/esp32
idf.py set-target esp32s3
idf.py flash monitor
```

The first build is slow (~10 minutes) because ScummVM's `configure` and
`make libs` run as part of the IDF build to produce the engine static
libraries. Subsequent builds are fast.

To produce a single merged image for distribution (the same
`scummvm-tdeck-full.bin` as the GitHub Releases asset), run from this
directory after a successful build:

```bash
python -m esptool --chip esp32s3 merge_bin -o scummvm-tdeck-full.bin \
    --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0      build/bootloader/bootloader.bin \
    0x8000   build/partition_table/partition-table.bin \
    0x10000  build/scummvm.bin \
    0xb10000 build/storage.bin
```

### Iterating without losing saves

`idf.py flash` rewrites all four partitions including the LittleFS
partition, which **wipes any saves and config**. While iterating on
firmware code, use:

```bash
idf.py app-flash
```

That writes only the app and leaves LittleFS (and your saves) alone.

Engines enabled
---------------

`scumm` (+ `he`), `agi`, `sky`, `queen`, `dreamweb`. Edit
`components/scummvm/CMakeLists.txt` to change the set. Heavy engines like
`sci`, `sword2` or `groovie` may not be playable due to CPU and RAM
constraints on the S3.

Bundled game data, themes and other assets fit in a 4 MB partition. To
include more games, drop them under `flash_data/games/<name>/`, optionally
add a pre-registration block in `main/main.cpp` (look for `monkey-demo`),
then rebuild and reflash. Anything bigger than the 4 MB partition needs
`partitions.csv` adjusted (shrink `factory`, grow `storage`).

Hardware notes
--------------

| Peripheral | Pins | Driver |
|---|---|---|
| Peripheral power rail | GPIO 10 (high to enable) | `tdeck_board.c` |
| Shared SPI bus (LCD + SD) | SCK 40, MOSI 41, MISO 38 | `tdeck_board.c` |
| ST7789 LCD | CS 12, DC 11, BL 42 | `esp-graphics.cpp` |
| MAX98357A I2S | BCLK 7, LRCK 5, DOUT 6 | `esp-mixer.cpp` |
| BBQ10 keyboard | I2C SCL 8, SDA 18, addr 0x55 | `tdeck_kbd.c` |
| Trackball | UP 3, DOWN 15, LEFT 1, RIGHT 2, CLICK 0 | `tdeck_trackball.c` |
| microSD | CS 39 (shared SPI) | `mmc.c` |

Known issues
------------

- Games larger than 320×240 are nearest-neighbour-downscaled and look
  pixelated. 320×200 games are letterboxed (black bars top and bottom).
- No volume control yet — output is at full I2S amplitude.
- Performance is borderline for SCUMM v6+ games. The starter engine set
  is the realistic ceiling on S3.
- No Wi-Fi, LoRa, GPS or power management.
- SD card detection on T-Deck v1 is flaky — works for the bundled
  LittleFS regardless.

The original ESP32-P4 backend (MIPI-DSI panel, PPA scaler, USB-HID
keyboard) lives on the `esp32p4-ev` branch if you want it.
