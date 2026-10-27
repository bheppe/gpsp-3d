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
- **Gyro sensor** for WarioWare: Twisted, using the 3DS gyroscope.
- **Forwarder support.** Started with a ROM path as its first argument, the
  app plays that game directly and closes when you quit.
- **Screen effects**: Scanlines and LCD grid (Display -> Screen effect).
- **HOME Menu icons per game** (forwarders), made right on the 3DS with
  **Icon Maker** (GBA and SNES games), or on a PC. See below.
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

1. Download `gpsp3d.cia` (or `gpsp3d.3dsx`) from the
   [Releases](../../releases) page.
2. **CIA:** copy it to the SD card and install it with FBI or another CIA
   installer. It appears on the HOME Menu as **gpSP 3D**.
   **3DSX:** copy it to `sdmc:/3ds/` and start it from the Homebrew Launcher.
3. Start gpSP 3D, browse to your games on the bottom screen and tap one.

To try changes before they are released, the [Nightly](../../releases/tag/nightly)
pre-release has the latest build of `main`, with QR codes for FBI. It is
replaced by every change, so it can be less stable than a release.

## Using it

- **GBA controls:** A, B, L, R, Start, Select, D-pad / Circle Pad.
- **Menu:** tap the bottom screen while playing. Tabs:
  **Game** (save/load state, slot, reset, quit), **Display** (screen size,
  scaling, color), **3D** (effect, strength, layer style, per-layer depths,
  sprite pop, focus, floor tilt, smart layers), **Speed** (frame skip, audio when slow,
  stats) and **System** (core 2 drawing, GPU display, core 2 priority).
- **3D:** raise the 3DS 3D slider. **3D tab -> Strength** scales the effect.
  **3D tab -> Layer style** picks a preset. **3D tab -> Settings for** chooses
  whether changes apply to all games or only this one.
- **Files** on the SD card:
  - `sdmc:/3ds/gpsp3d/gpsp3d.cfg`: settings
  - `sdmc:/3ds/gpsp3d/games/<game>.cfg`: per-game 3D profiles
  - Game saves (`.srm`) and states (`.state1` ...): RetroArch's save folder
    if present, otherwise next to the ROM

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

1. Install `iconmaker.cia` from the [Releases](../../releases) page with FBI
   (or run `iconmaker.3dsx` from the Homebrew Launcher).
2. Open **Icon Maker**, go to your games and press **A** on one. The top screen
   shows the icon and banner it will get; press **A** again to put it on the
   HOME Menu. **Y** does every game in the folder at once.
3. **My icons** (R, or tap the tab) lists the icons you've made; **X** deletes
   one (the game file stays).

Without any pictures, a game gets a cartridge icon with its name on the label
and a banner with the icon and its name. To use your own, put them next to the
game: `Pokemon Emerald.png` for the icon (any size, best square) and
`Pokemon Emerald.banner.png` for the banner (best 256x128). Making a game again
replaces its icon. More in [iconmaker/README.md](iconmaker/README.md).

### On a PC: make-forwarders.sh

1. Put the SD card in your PC (or copy the games folder over).
2. In the devkitPro shell, run, with your own folders:
   ```
   bash gpsp3ds/make-forwarders.sh /e/roms/gba /roms/gba
   ```
   The first folder is where the games are on the PC, the second is the same
   folder on the 3DS SD card.
3. Install the CIAs from `gpsp3ds/out/forwarders/` with FBI.

For a custom icon, put a 48x48 PNG with the game's name next to it
(`Pokemon Emerald.gba` -> `Pokemon Emerald.png`). For a custom banner (the
picture on the top screen when the icon is selected), add a 256x128 PNG named
like the game plus `.banner` (`Pokemon Emerald.banner.png`), and optionally a
short WAV for its sound (`Pokemon Emerald.banner.wav`). Other forwarder tools that
start `gpsp3d.3dsx` with the game's path as its argument also work.

## Building

You need [devkitPro](https://devkitpro.org/wiki/Getting_Started) with the 3DS
packages (`3ds-dev`, which includes libctru, citro3d and picasso), plus
`makerom` and `bannertool` in your PATH.

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
with `3ds-dev`, plus `makerom` and `bannertool` at pinned versions). Open the
repo in VS Code with the Dev Containers extension (Docker or Podman) and choose
**Reopen in Container**, or start a GitHub Codespace. The first start builds
the image, which takes a few minutes. Then, in the container's terminal:

```
bash gpsp3ds/build-gpsp3ds.sh
bash iconmaker/build-iconmaker.sh
```

The image is defined in `docker/gpsp3d-devcontainer.Dockerfile`.
`bash docker/ci-build.sh` builds both apps and checks a forwarder made by Icon
Maker, the same test build GitHub Actions runs for every pull request and every
push to `main`.

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

Game Boy Advance and Nintendo 3DS are trademarks of Nintendo. This project is
not affiliated with or endorsed by Nintendo.
