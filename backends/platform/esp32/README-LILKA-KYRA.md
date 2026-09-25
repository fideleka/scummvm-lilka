# Lilka v2 Kyra engine notes

The general `feature/lilka-scummvm` branch builds ScummVM's `kyra` engine as
its own image alongside the SCUMM image. It is intended for *The Legend of Kyrandia* (game ID
`kyra1`). Game data is not included.

Physical A (GPIO5) is the primary left click and B (GPIO6) is the secondary
right click when using the example manifest's A/B actions.

Build **both** implemented engines with ESP-IDF 5.3.2 using
`./compile-all-lilka.sh` from the repository root. It builds `scumm` and
`kyra` sequentially in isolated Git worktrees and writes
`build/lilka-engines/scumm.bin`, `build/lilka-engines/kyra.bin`, and
`build/lilka-engines/kyra.dat`, plus checksums. `IDF_PATH` may point to a
non-default ESP-IDF 5.3.2 installation. To build just one engine, use
`./compile-lilka.sh scumm` or `./compile-lilka.sh kyra` from a clean checkout.
When `/mnt/d/Software/scummvm-lilka` exists (WSL), the script also copies
both engine images and `kyra.dat` there. `LILKA_WINDOWS_COPY_DIR` can override
that destination.
Each raw image is checked against the 0x640000-byte Lilka guest-slot limit.
Do not flash the T-Deck merged image or the partition table to Lilka.

Copy the image to `/sd/scummvm/engines/kyra.bin`. Also copy
`dists/engine-data/kyra.dat` to `/sd/scummvm/data/engine-data/kyra.dat`.
The guest also accepts `kyra.dat` beside `kyra.bin` under
`/sd/scummvm/engines/`; the shared engine-data folder takes precedence.
Place your own compatible game files and a `.scummvm` manifest in a game
folder. The Keira example is `docs/examples/legend-of-kyrandia.scummvm` on
its `feature/scummvm-manager` branch. Its `engine: "kyra"` field selects
`kyra.bin` from Keira's fixed registry; the Kyra guest accepts only that
image's RTC command and revalidates the manifest as Kyra before launching.

Kyra has reached its opening movie on Lilka. The corrected physical A/B order,
longer gameplay, saves across restart, and return to Keira still need a device
test after Anton builds the updated source.
