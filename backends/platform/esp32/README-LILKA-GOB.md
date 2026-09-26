# Lilka v2 Gob engine notes

The `feature/lilka-scummvm` branch can build ScummVM's `gob` engine as a
separate `gob.bin` guest image. *Gobliiins* has game ID `gob1`. Game data is
not included, and this engine does not need a separate `gob.dat` file.

Build it with `./compile-lilka.sh gob`, or include it with the other engines
using `./compile-all-lilka.sh`. Copy `gob.bin` to
`/sd/scummvm/engines/gob.bin`. Place your own compatible game files and the
Keira example `docs/examples/gobliiins.scummvm` in the same game folder.
Physical A is the example's primary left click; B is secondary right click.

Keira's `feature/scummvm-manager` branch has the matching fixed `gob` image
registry entry. The guest checks both the RTC image command and the manifest's
engine field. The raw image must fit the 0x640000-byte guest slot; the build
script checks this. Gob has not yet been built or tested on Lilka.
