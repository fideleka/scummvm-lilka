# Lilka v2 Kyra-only guest

This branch builds ScummVM's `kyra` engine separately from the existing
SCUMM-only guest. It is intended for *The Legend of Kyrandia* (game ID
`kyra1`). Game data is not included.

Build **both** implemented engines with ESP-IDF 5.3.2 using
`./compile-all-lilka.sh` from the repository root. It builds `scumm` and
`kyra` sequentially in isolated Git worktrees and writes
`build/lilka-engines/scumm.bin`, `build/lilka-engines/kyra.bin`, and
`build/lilka-engines/kyra.dat`, plus checksums. `IDF_PATH` may point to a
non-default ESP-IDF 5.3.2 installation. To build just one engine, use
`./compile-lilka.sh scumm` or `./compile-lilka.sh kyra` from a clean checkout.
Each raw image is checked against the 0x640000-byte Lilka guest-slot limit.
Do not flash the T-Deck merged image or the partition table to Lilka.

Copy the image to `/sd/scummvm/engines/kyra.bin`. Also copy
`dists/engine-data/kyra.dat` to `/sd/scummvm/data/engine-data/kyra.dat`.
Place your own compatible game files and a `.scummvm` manifest in a game
folder. The Keira example is `docs/examples/legend-of-kyrandia.scummvm` on
its `feature/scummvm-manager` branch. Its `engine: "kyra"` field selects
`kyra.bin` from Keira's fixed registry; the Kyra guest accepts only that
image's RTC command and revalidates the manifest as Kyra before launching.

This image has not yet been tested on Lilka. Boot, gameplay, controls, saves,
and return to Keira require a device test.
