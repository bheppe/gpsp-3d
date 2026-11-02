# Icon Maker (GBA + SNES)

Puts GBA and SNES games on the HOME Menu right from the 3DS, no PC needed.
GBA games open in gpSP 3D, SNES games (`.sfc`, `.smc`, `.fig`, `.swc`, or a `.zip` of one)
in [Snes9x 3D](https://github.com/bheppe/snes9x_3ds).

1. Install `iconmaker.cia` with FBI (or run `iconmaker.3dsx` from the Homebrew Launcher).
   gpSP 3D (and Snes9x 3D, for SNES games) must be installed as a CIA. Snes9x 3D needs
   the version that reads `sdmc:/3ds/snes9x_3ds/launch.txt` at startup.
2. Optional: put a PNG or JPEG next to a game with the same name (`Golf.gba` ->
   `Golf.png` or `Golf.jpg`), any size. It becomes the icon; without one the icon
   is a cartridge with the game's name on it, colored by the name. Making a game again replaces its icon.
   The top screen banner is 3D by default: the icon on a rounded tile that turns and
   bobs, the game's name under it, and a soft chime. SELECT on the Games tab switches
   between that and a flat banner (blue, or no background), and the choice is saved.
   A game with its own banner picture, `Golf.banner.png` or `.jpg` (best 256x128),
   always gets that picture as its banner, also when 3D is chosen (3D is for the
   games without one). A flat banner without one is the icon, bigger, with the name.
   A picture of another shape (most box art isn't square) is padded to fit by
   default: the margins are white on the icon (HOME Menu icons have no
   transparency), the tile's grey on the 3D banner, and see-through on a flat
   banner. X on the Games tab switches to cropping (scaled to fill, the edges
   cut off) for the icon, the 3D tile and the banner alike; the choice is saved.
   Covers can be big: PNGs and JPEGs are read a piece at a time and shrunk
   while they're read, so even a 15 MB cover needs little memory. Only
   pictures over 4096 pixels on a side, interlaced PNGs whose full picture
   would take over 8 MB, and progressive JPEGs over about 1600 to 2300
   pixels a side (depending on how they were saved) can't be used: the
   preview says "picture too big" and the icon is made from the name.
3. Open Icon Maker, pick the game, press A twice. Its icon appears on the HOME Menu
   and opens gpSP 3D straight into the game.

L downloads a game's box art from
[libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
(the covers RetroArch shows) and saves it as its picture, `Golf.png` next to
the game (needs Wi-Fi). The game is found in libretro's No-Intro list by its
file name (`Golf (USA).gba`), else by its CRC32: a zip's is read from the zip,
any other game is read whole once (a few seconds for a big GBA game). If its
region has no cover, the same game from another region is used. L on a folder
row (or `..`) does every game in the folder without a picture; hold B to stop.
A game that has its own picture keeps it. The picture is saved small (128
pixels on its shorter side, all the icon and the 3D tile need), so it's quick
to read when you make icons later.

To keep the pictures out of your games folders, set a folder in
`sdmc:/3ds/gpsp3d/iconmaker.cfg`:

```
boxart_dir=sdmc:/3ds/gpsp3d/boxart
```

Pictures are then saved there and found there by the game's name (a picture
next to the game still wins). libretro's game lists are kept in
`sdmc:/3ds/gpsp3d/boxart_gba.txt` and `boxart_snes.txt`; delete them to get
newer ones.

### Your own box art libraries

For games libretro doesn't have (ROM hacks, translations, homebrew), add
your own libraries, one `boxart_source=` line each, in `iconmaker.cfg`:

```
boxart_source=https://raw.githubusercontent.com/you/hack-covers/main/
boxart_source=https://example.com/more-covers/
```

They're tried in that order, then libretro's. A library is laid out like
libretro-thumbnails, a folder per system:

```
Nintendo_-_Game_Boy_Advance/
  list.txt
  Named_Boxarts/
    Pokemon - Radical Red (USA) [Hack by soupercell].png
Nintendo_-_Super_Nintendo_Entertainment_System/
  list.txt
  Named_Boxarts/...
```

- `list.txt` (optional) has a line per game: its CRC32, a space, its name.
  For a hack, the CRC32 of the patched ROM; one line for each version, all
  with the same name if they share a picture:
  ```
  1A2B3C4D Pokemon - Radical Red (USA) [Hack by soupercell]
  ```
  Without a `list.txt`, a game is found by its file name only
  (`My Hack.gba` -> `Named_Boxarts/My Hack.png`).
- Pictures are PNGs (at most 512 pixels wide, as libretro's). In their file
  names ``& * / : ` < > ? \ | "`` are `_`.
- Names as libretro writes them (`Title (Region) [Hack by Author]`,
  `[T-En by Translator]`) let pictures go to libretro-thumbnails later
  unchanged.

A library's list is kept in `sdmc:/3ds/gpsp3d/boxart_<number>_gba.txt`
(delete it after changing `list.txt`). A library that can't be reached is
left out until the next L.

Y on a folder's list makes icons for every game in it. Games that have an icon
already are marked `*` in the list.

R opens **My icons**, the games you've put on the HOME Menu: X deletes an icon
(the game file stays). Icons made on a PC with `make-forwarders.sh` show up there too.

Needs custom firmware with signature patches (Luma3DS), as for any CIA.

## Building

In the devkitPro MSYS2 shell: `./build-iconmaker.sh`. It builds the template
forwarder (from `../gpsp3ds/forwarder`, title ID 000400000F3D0000, unencrypted)
into `data/fwd_gpsp3d.bin`, then the app into `out/`.

It also builds a second template with the 3D banner (`res/banner3d.cgfx` and
`res/jingle.wav`, made by `tools/make_banner3d.py`, which also makes gpSP 3D's
and Icon Maker's own 3D banners; it needs [pycgfx](https://github.com/skyfloogle/pycgfx),
gltflib and Pillow: `python3 tools/make_banner3d.py /path/to/pycgfx`).

How it works: `source/fwdcia.c` copies the template, gives it a title ID made from
the game's path, writes the game's path into the forwarder's slot, sets the name
and icon, and redoes every hash the 3DS checks. `test/` checks this on a PC
(`fwdtest` patches a CIA, `verify.py` checks every hash independently;
`pngtest` and `jpegtest` shrink a PNG or JPEG as Icon Maker does, with the
PC's libpng and libjpeg; `boxarttest game.gba out.png` gets a game's box art
as Icon Maker does, with the PC's libcurl).
