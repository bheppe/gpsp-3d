# Icon Maker (GBA + SNES)

Puts GBA and SNES games on the HOME Menu right from the 3DS, no PC needed.
GBA games open in gpSP 3D, SNES games (`.sfc`, `.smc`, `.fig`, `.swc`, or a `.zip` of one)
in [Snes9x 3D](https://github.com/bheppe/snes9x_3ds).

1. Install `iconmaker.cia` with FBI (or run `iconmaker.3dsx` from the Homebrew Launcher).
   gpSP 3D (and Snes9x 3D, for SNES games) must be installed as a CIA. Snes9x 3D needs
   the version that reads `sdmc:/3ds/snes9x_3ds/launch.txt` at startup.
2. Optional: put a PNG next to a game with the same name (`Golf.gba` -> `Golf.png`),
   best square. It becomes the icon; without one the icon is a cartridge
   with the game's name on it, colored by the name. Making a game again replaces its icon.
   Same for the top screen banner: `Golf.banner.png` (best 256x128; other sizes are
   fitted). Without one, the banner is the icon, bigger, with the game's name.
3. Open Icon Maker, pick the game, press A twice. Its icon appears on the HOME Menu
   and opens gpSP 3D straight into the game.

Y on a folder's list makes icons for every game in it. Games that have an icon
already are marked `*` in the list.

R opens **My icons**, the games you've put on the HOME Menu: X deletes an icon
(the game file stays). Icons made on a PC with `make-forwarders.sh` show up there too.

Needs custom firmware with signature patches (Luma3DS), as for any CIA.

## Building

In the devkitPro MSYS2 shell: `./build-iconmaker.sh`. It builds the template
forwarder (from `../gpsp3ds/forwarder`, title ID 000400000F3D0000, unencrypted)
into `data/fwd_gpsp3d.bin`, then the app into `out/`.

How it works: `source/fwdcia.c` copies the template, gives it a title ID made from
the game's path, writes the game's path into the forwarder's slot, sets the name
and icon, and redoes every hash the 3DS checks. `test/` checks this on a PC
(`fwdtest` patches a CIA, `verify.py` checks every hash independently).
