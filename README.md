# gpSP 3D

**Game Boy Advance games in stereoscopic 3D on the Nintendo 3DS, at full speed on an Old 3DS.**

gpSP 3D is a standalone 3DS app built on [gpSP](https://github.com/libretro/gpsp)
(the libretro version). It puts each GBA background layer and sprite group at
its own depth, so maps, characters, HUDs and backgrounds separate when you
raise the 3D slider. No RetroArch needed.

**Discord** (help, game reports, test builds): https://discord.gg/pBWY3fRgnU

> This repository is a fork of [libretro/gpsp](https://github.com/libretro/gpsp).
> The original gpSP readme is in [README.gpsp.md](README.gpsp.md).

## Features

- **Per-layer 3D.** Each GBA layer (BG0-BG3, sprites by priority) gets its own
  depth. Presets: Classic, Pop-out, Deep, Gentle, Flat map, or Custom with a
  slider per layer.
- **Smart layers.** Layers that scroll together and share tile graphics (one
  picture split over several layers, as in Pokemon maps) are kept at one depth,
  so hidden filler tiles never show through.
- **Floor tilt.** Brings the bottom of the screen nearer and pushes the top
  further away, so roads in racing games (and floors in general) recede.
- **Per-game 3D profiles.** Settings can be saved for one game. Pokemon games
  start on Deep with Smart layers automatically.
- **Full speed on an Old 3DS.** gpSP's ARM dynarec runs on the main core.
  Picture drawing runs on the second CPU core, so emulation and sound stay at
  60 fps. In 3D, the picture refreshes at about 30 per second on an Old 3DS.
- **GPU display** with three scaling modes: Sharp, Crisp (sharp bilinear) and
  Smooth. Screen sizes: Original (1:1), Fit (1.5x) and Stretch.
- **Color styles**: Original, GBA soft, GBA screen, plus Saturation and
  Brightness.
- **Touch menu** on the bottom screen with a live preview, save states with
  slots, auto frame skip, audio stretch when the game runs slow, and a stats
  panel.
- **Gyro and tilt sensors**: the 3DS gyroscope for WarioWare: Twisted, the
  accelerometer for Yoshi Topsy-Turvy and Koro Koro Puzzle Happy Panechu.
- **A calm bottom screen while you play**: the game's name and "Tap the screen
  for the menu", or the performance stats, or the backlight off after 10 s
  (**System -> Bottom screen**).
- **No SD card writes while playing.** The log and the profiler keep what
  they write in memory until the menu opens or the game closes.
- **Forwarder support.** Started with a ROM path as its first argument, the
  app plays that game directly and closes when you quit.
- **Screen effects**: Scanlines and LCD grid (Display -> Screen effect).
- **HOME Menu icons per game** (forwarders), made right on the 3DS with
  **Icon Maker** (GBA and SNES games), or on a PC. Their banner is 3D: the
  game's icon on a turning tile that comes out of the screen, its name under
  it, and a soft chime. See below.
- **Game guides on the bottom screen** while you play (a `.txt` next to the
  game).
- **Fast forward** (Y) and **zipped games**.
- **Saves are shared with RetroArch.** Saves go to RetroArch's save folder if
  it exists, so you can switch between the two.

## Requirements

- A Nintendo 3DS / 2DS family console (Old or New) with custom firmware
  ([Luma3DS](https://github.com/LumaTeam/Luma3DS)). The 3D effect needs a model
  with a 3D screen.
- `sdmc:/3ds/dspfirm.cdc` for sound. Dump it with
  [DSP1](https://github.com/zoogie/DSP1). Without it the app runs silently.
- Your own legally dumped GBA games. None are included or linked here.
- No GBA BIOS file is needed. gpSP has a built-in open-source BIOS.

## Install

There are three ways in. All need a 3DS with custom firmware and FBI or the
Homebrew Launcher; [3ds.hacks.guide](https://3ds.hacks.guide/) sets that up.

### Universal-Updater (recommended)

<img src="https://bheppe.github.io/gpsp-3d/unistore/gpsp3d-unistore-QR.png" alt="QR code of the gpSP 3D UniStore" width="200" align="right">

1. In [Universal-Updater](https://github.com/Universal-Team/Universal-Updater),
   open **Settings** -> **Select a UniStore** -> **+**.
2. Scan the QR code, or type the store address:
   `https://bheppe.github.io/gpsp-3d/unistore/gpsp3d.unistore`
3. Select the gpSP 3D store, open the **gpSP 3D** entry and pick a download:
   - **gpsp3d.cia** installs it to the HOME Menu.
   - **gpsp3d.3dsx** puts it in the Homebrew Launcher.

The store also has **Icon Maker** and **Snes9x 3D**. New releases show up in
it by themselves, and an entry is flagged when an update is available.
Earlier versions stay listed as `[v0.1.3] gpsp3d.cia` and so on, should you
need to go back.

### FBI

Every [release](../../releases) has QR codes for `gpsp3d.cia` and
`iconmaker.cia` in its notes. In FBI, choose **Remote Install** ->
**Scan QR Code** and scan one.

### By hand

Download from the [latest release](../../releases/latest):

- `gpsp3d.cia`: copy it to the SD card and install it with FBI or another CIA
  installer. It appears on the HOME Menu as **gpSP 3D**.
- `gpsp3d.3dsx`: copy it to `sdmc:/3ds/` and start it from the Homebrew
  Launcher.

### After installing

Start gpSP 3D, browse to your games on the bottom screen and tap one.

Updating or reinstalling keeps your settings and per-game profiles: they live
in `sdmc:/3ds/gpsp3d/` on the SD card, and saves stay where they are (see
**Files** below).

To try changes before they are released, the [Nightly](../../releases/tag/nightly)
pre-release has the latest build of `main`, with QR codes for FBI. It is
replaced by every change, so it can be less stable than a release. It is not
in the store.

## Using it

- **GBA controls:** A, B, L, R, Start, Select, D-pad / Circle Pad.
- **Fast forward:** hold **Y** (or set it to toggle in **Speed -> Fast forward**).
  **Speed -> Fast forward speed** picks Max, 2x, 3x or 4x. Light games reach 2-4x
  on an Old 3DS; a New 3DS has more headroom.
- **Zipped games** (`.zip`) work: the game is unzipped once to
  `sdmc:/3ds/gpsp3d/unzipped/` (the last one played is kept, so starting it again
  is instant). Saves are still named after the zip and kept next to it.
- **Guides:** put a plain-text guide (a walkthrough or FAQ saved as `.txt`) next
  to the game with the same name (`Pokemon Emerald.gba` -> `Pokemon Emerald.txt`),
  or in `sdmc:/3ds/gpsp3d/guides/`. Then **Game -> Show guide** shows it on the
  bottom screen while you play: drag to scroll, drag the bar on the right to
  jump, **Page up / Page down** at the bottom, **Menu** at the top right opens
  the menu. It remembers where you were, and whether it was open, per game.
  Guides up to 3 MB; accented letters and fancy quotes show as plain ones.
- **Menu:** tap the bottom screen while playing. Tabs:
  **Game** (save/load state, slot, reset, guide, quit), **Display** (screen size,
  scaling, color), **3D** (effect, strength, layer style, per-layer depths,
  sprite pop, focus, floor tilt, smart layers), **Speed** (frame skip, audio when slow,
  fast forward, stats) and **System** (core 2 drawing, GPU display, core 2 priority,
  **Real-time clock** (Auto / On / Off; On for Pokemon hacks whose clock isn't found),
  **Startup** (skip or play the GBA intro, which needs your own `gba_bios.bin` in
  `sdmc:/3ds/gpsp3d/`), **Bottom screen** (Simple / Performance / Off after 10 s),
  **Save file** (where the current game's save is) and **Credits**).
- **3D:** raise the 3DS 3D slider. **3D tab -> Strength** scales the effect.
  **3D tab -> Layer style** picks a preset. **3D tab -> Settings for** chooses
  whether changes apply to all games or only this one.
- **Files** on the SD card:
  - `sdmc:/3ds/gpsp3d/gpsp3d.cfg`: settings
  - `sdmc:/3ds/gpsp3d/games/<game>.cfg`: per-game 3D profiles
  - Game saves (`.srm`) and states (`.state1` ...): RetroArch's save folder
    if present, otherwise next to the ROM. A game that already has a save next
    to it (`.srm`, or `.sav` with `save_ext=sav`) keeps using that one.
    **System -> Save file** shows where the current game's save is.
- **Own save folder:** to share saves with mGBA or open_agb_firm, set these
  in `gpsp3d.cfg` (empty = as above):

  ```
  save_dir=sdmc:/3ds/open_agb_firm/saves
  state_dir=sdmc:/3ds/gpsp3d/states
  save_ext=sav
  ```

  The folders are created if missing, and states go with the saves unless
  `state_dir` is set. A game's save that isn't in the new folder yet is
  loaded from the old place (the old file is kept) and written to the new
  folder the next time the game saves. With an extension other than `srm`,
  files have the cartridge's real save size (32 KB SRAM, 64/128 KB flash,
  512 B / 8 KB EEPROM) like mGBA's. States don't move: copy them over.

If the app ever freezes while starting the GPU display, the next launch shows
a message and turns **System -> GPU display** off, so you can always get back
in.

## HOME Menu icons for your games

You can give each game its own HOME Menu icon that starts gpSP 3D straight
into it (a "forwarder"). gpSP 3D must be installed as a CIA.

### On the 3DS: Icon Maker

Icon Maker does GBA games (opened in gpSP 3D) and SNES games (`.sfc`, `.smc`,
`.fig`, `.swc`, opened in [Snes9x 3D](https://github.com/bheppe/snes9x_3ds)
v1.61-layers6 or newer, installed as a CIA). SNES games get a grey Super
Nintendo cartridge icon.

1. Install **Icon Maker** from the [store](#universal-updater-recommended),
   or `iconmaker.cia` from the [Releases](../../releases) page with FBI
   (or run `iconmaker.3dsx` from the Homebrew Launcher).
2. Open **Icon Maker**, go to your games and press **A** on one. The top screen
   shows the icon and banner it will get; press **A** again to put it on the
   HOME Menu. **Y** does every game in the folder at once.
3. **My icons** (R, or tap the tab) lists the icons you've made; **X** deletes
   one (the game file stays).

The banner is 3D by default: the game's icon on a rounded tile that turns and
bobs in front of the HOME Menu's theme, the game's name under it, and a soft
chime. **SELECT** on the Games tab switches to a flat banner (gpSP 3D's blue, or
no background) and back; the bottom screen shows which is on.

Without any pictures, a game gets a cartridge icon with its name on the label.
To use your own, put them next to the game: `Pokemon Emerald.png` (or `.jpg`)
for the icon (any size; it's also what goes on the 3D tile) and
`Pokemon Emerald.banner.png` (or `.jpg`) for a flat banner (best 256x128).
A picture of another shape, like most box art, is **padded** to fit by default;
**X** on the Games tab switches to **cropping** it (scaled to fill, the edges
cut off) and back. This applies to the icon, the 3D tile and the banner, and
the choice is saved. Making a game again replaces its icon. More in
[iconmaker/README.md](iconmaker/README.md).

### On a PC: make-forwarders.sh

1. Put the SD card in your PC (or copy the games folder over).
2. In the devkitPro shell, run, with your own folders:
   ```
   bash gpsp3ds/make-forwarders.sh /e/roms/gba /roms/gba
   ```
   The first folder is where the games are on the PC, the second is the same
   folder on the 3DS SD card.
3. Install the CIAs from `gpsp3ds/out/forwarders/` with FBI.

For a custom icon, put a picture with the game's name next to it
(`Pokemon Emerald.gba` -> `Pokemon Emerald.png`, `.jpg` or `.jpeg`). For a
custom banner (the picture on the top screen when the icon is selected), add
one named like the game plus `.banner` (`Pokemon Emerald.banner.png`), and
optionally a short WAV for its sound (`Pokemon Emerald.banner.wav`).

With [ImageMagick](https://imagemagick.org) installed
(`pacman -S mingw-w64-x86_64-imagemagick` in the devkitPro shell), pictures
can be JPEGs and any size. One of another shape than the icon (48x48) or the
banner (256x128) is padded to fit (`--fit pad`, the default) or scaled to
fill with the edges cut off (`--fit crop`):
```
bash gpsp3ds/make-forwarders.sh --fit crop /e/roms/gba /roms/gba
```
The bars are see-through on the banner and white on the icon (HOME Menu
icons can't be see-through); `--pad-color black` (or `'#1e2327'`, ...) colors
both. Transparent parts of an icon are drawn on white. Without ImageMagick,
use PNGs of the exact size (48x48, 256x128). Other forwarder tools that
start `gpsp3d.3dsx` with the game's path as its argument also work.

## Building

You need [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS
packages (`3ds-dev`, which includes libctru, citro3d and picasso) and, for Icon
Maker, `3ds-libjpeg-turbo` and `3ds-libpng` (`dkp-pacman -S 3ds-libjpeg-turbo
3ds-libpng`), plus `makerom` and `bannertool` in your PATH.

```
git clone https://github.com/bheppe/gpsp-3d
bash gpsp-3d/gpsp3ds/build-gpsp3ds.sh
```

The script builds the gpSP core (`make platform=ctr`), then the app, then the
CIA. Results go to `gpsp3ds/out/` (`gpsp3d.cia`, `gpsp3d.3dsx`).

Icon Maker: `bash gpsp-3d/iconmaker/build-iconmaker.sh` (results in
`iconmaker/out/`).

### Building in a devcontainer

The repo has a devcontainer with everything above already installed (devkitARM
with `3ds-dev`, `3ds-libjpeg-turbo` and `3ds-libpng`, plus `makerom` and `bannertool` at pinned versions). Open the
repo in VS Code with the Dev Containers extension (Docker or Podman) and choose
**Reopen in Container**, or start a GitHub Codespace. The first start builds
the image, which takes a few minutes. Then, in the container's terminal:

```
bash gpsp3ds/build-gpsp3ds.sh
bash iconmaker/build-iconmaker.sh
```

The image is defined in `docker/gpsp3d-devcontainer.Dockerfile`; CI uses the
published copy tagged with a hash of that file (`bash docker/build-image.sh`).
`bash docker/ci-build.sh` builds both apps and checks a forwarder made by Icon
Maker, the same test build GitHub Actions runs for every pull request and every
push to `main`.

The `nightly` tag moves with every push to `main`, so `git pull` may stop with
`would clobber existing tag`; `git fetch --tags --force` fixes it.

### What changed in the core

- `video_thread.cc` / `video_thread_sys.c`: a threaded renderer. The main core
  records each scanline's registers, palette, OAM and changed VRAM blocks, and
  the second core draws the lines. It is also where stereo 3D happens: each eye
  is drawn with every layer shifted by its depth, with Smart grouping and gap
  filling.
- `video.cc`: hooks for per-eye sprite shifts and layer tracking. The normal
  renderer is unchanged when these aren't used.
- `gba_memory.c`, `arm/arm_stub.S`, `x86/x86_stub.S`, `cpu.h`: VRAM and
  palette dirty tracking (per 4 KB block) for the renderer's snapshots, and a
  fix for a stale ROM page queue when a second game is loaded.
- `profiler.c`: optional timing profiler (`PROFILE=1`).

## Support

gpSP 3D is free and always will be: no paywalled features, ever.
It's built with the help of AI coding tools, which cost real money per
session. If it made your GBA games better, a tip helps keep fixes and
new features coming: https://ko-fi.com/bheppe

Bug reports and game test results help just as much. Post them under
[Issues](../../issues) or on [Discord](https://discord.gg/pBWY3fRgnU). What's done and what's planned is in [ROADMAP.md](ROADMAP.md).

## Credits and licenses

- **gpSP** by Exophase, with later work by notaz, davidgfnet and the libretro
  team. GPL-2.0-or-later, see [COPYING](COPYING).
- **gpSP 3D** (3DS app, threaded renderer, stereo 3D) by bheppe. GPL-2.0-or-later.
- `gpsp3ds/source/ctr-gpu.c`, `ctr-gpu.h` and `gpsp3ds/res/uishader.*.pica`
  come from [mGBA](https://github.com/mgba-emu/mgba) (Yuri Kunde Schlesner,
  Jeffrey Pfau), under MPL-2.0 (see the file headers).
- `gpsp3ds/source/ctr_svchax.c` is from
  [RetroArch](https://github.com/libretro/RetroArch) (GPL-3.0), originally
  [svchax](https://github.com/aliaspider/svchax) by aliaspider. Because of this
  file, the 3DS app binary as a whole is distributed under GPL-3.0.
- UI font: [Inter](https://rsms.me/inter/) by Rasmus Andersson, SIL Open Font
  License 1.1 (`gpsp3ds/res/Inter-LICENSE.txt`).
- The 3D banner model is made by `iconmaker/tools/make_banner3d.py` with
  [pycgfx](https://github.com/skyfloogle/pycgfx) by skyfloogle (glTF to CGFX;
  run on a PC, not part of the apps). The fixes that make such a model work on
  the real HOME Menu follow [ClouDS Music FA](https://github.com/Epic0522/ClouDS-Music-FA)'s
  and [ftb](https://github.com/fosterbarnes/ftb)'s banner converters.

Game Boy Advance and Nintendo 3DS are trademarks of Nintendo. This project is
not affiliated with or endorsed by Nintendo.
