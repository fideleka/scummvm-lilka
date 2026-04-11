
ScummVM for LilyGo T-Deck (ESP32-S3)
====================================

This is a fork of [ScummVM](https://github.com/scummvm/scummvm) that can build
a ScummVM backend for the [LilyGo T-Deck v1](https://github.com/Xinyuan-LilyGO/T-Deck)
(ESP32-S3, 16 MB flash, 8 MB octal PSRAM, 320x240 ST7789 SPI display,
MAX98357A I2S speaker amp, BlackBerry Q10 I2C keyboard, optical trackball,
microSD). The backend code is located in `/backends/platform/esp32`.

The original ESP32-P4 backend (MIPI-DSI, PPA scaler, USB-HID keyboard) lives
on the `esp32p4-ev` branch if you want it.

Building
--------

You need ESP-IDF 5.3 or newer. With it active:

```
cd backends/platform/esp32
idf.py set-target esp32s3
idf.py flash monitor
```

Building is slow the first time because ScummVM's `configure` and `make libs`
run as part of the IDF build. The final binary is around 10 MiB.

Preparing the SD card
---------------------

You need a microSD card (FAT-formatted) containing both the ScummVM support
files and any games you want to run. To build the support files, from the
ScummVM root run:

```
make esp32dist
```

This generates an `esp32dist/scummvm` folder. Copy that `scummvm` folder to
the root of the SD card. Put game folders anywhere else on the card — the
GUI file browser will find them.

Freeware games known to at least start on this backend:
*Beneath a Steel Sky*, *Dreamweb*, *Flight of the Amazon Queen*. More can be
enabled by editing the engine list in
`backends/platform/esp32/components/scummvm/CMakeLists.txt`.

Input
-----

- **BBQ10 keyboard**: ASCII typing, enter, backspace, space, arrow-key glyphs.
- **Speaker key** (top-left extra button) → F5 (save menu in LucasArts games).
- **Mic key** → F7 (load menu).
- **Sym / alt key** → Alt modifier.
- **Trackball**: moves the mouse cursor. Center click = left mouse button.

Engines enabled by default
--------------------------

`scumm` (+ `he`), `agi`, `sky`, `queen`, `dreamweb`. Edit
`components/scummvm/CMakeLists.txt` to change the set. Note that adding heavy
engines like `sci`, `sword2` or `groovie` may not be playable due to CPU and
RAM constraints on the S3.

Known issues
------------

- Games larger than 320x240 are downscaled with nearest-neighbour filtering
  and look pixelated. True 320x200 games are letterboxed (20 px black bars
  top/bottom).
- No volume control yet (software scaling TODO).
- No Wi-Fi, LoRa, GPS or power management wiring.
