# AGENTS.md

Context for AI coding agents (Claude Code, Codex, ...) working on this repo.
Read it before changing anything; the README is for users, this is for you.

## What this is

- **gpSP 3D** (`gpsp3ds/`): a standalone Nintendo 3DS app around the gpSP GBA
  core (this repo's root, a fork of libretro's gpSP), with per-layer
  stereoscopic 3D. Target: full speed on an **Old 3DS**.
- **Icon Maker** (`iconmaker/`): a 3DS app that makes HOME Menu forwarders
  (CIAs) for GBA games (start gpSP 3D) and SNES games (start
  [Snes9x 3D](https://github.com/bheppe/snes9x_3ds), a separate repo).
- Maintainer: bheppe. Releases go out through GitHub Releases, a
  Universal-Updater store (GitHub Pages, `docs/` + `.github/unistore.jq`),
  Reddit and Discord. Contributors: michvllni (CI, Pages/store, Icon Maker
  pictures and speed).

## Layout

| Path | What |
|---|---|
| `*.c`, `*.cc`, `arm/`, `video_thread*.cc` | the gpSP core (libretro), built as `libgpsp.a` with `platform=ctr` |
| `gpsp3ds/source/main.c` | the app: menu, settings, game loop, audio, GPU display, 3D, update check, button mapping |
| `gpsp3ds/source/ui*.c` | software UI on the bottom screen (fonts in `ui_font.c`, made by `gpsp3ds/tools/make_font.py`) |
| `gpsp3ds/source/ctr-gpu.*`, `res/uishader.*.pica` | from mGBA (MPL-2.0) |
| `gpsp3ds/forwarder/` | the tiny forwarder program every forwarder CIA runs |
| `gpsp3ds/make-forwarders.sh` | PC script: forwarder CIAs for a folder of games (ImageMagick optional) |
| `iconmaker/source/fwdcia.c`, `banner.c` | patch a template CIA on the 3DS (title ID, SMDH, banner, romfs path, every hash) |
| `iconmaker/source/pngshrink.c`, `jpegshrink.c` | covers decoded small while read (libpng / libjpeg) |
| `iconmaker/source/boxart.c`, `netget.c` | box art download: libretro's No-Intro list, CRC32, libretro-thumbnails (libcurl) |
| `iconmaker/tools/make_banner3d.py` | makes the 3D HOME Menu banners (needs pycgfx; run on a PC) |
| `iconmaker/test/` | `fwdtest.c` + `verify.py` (forwarder CIA checked like the 3DS does), `pngtest.c`, `jpegtest.c`, `boxarttest.c` |
| `tests/gba-roms/` | core ROM tests (CI) |
| `docker/` | the build image and `ci-build.sh` (what CI runs) |
| `ROADMAP.md` | what's done, in testing, planned; update it with every change |

## Building

devkitARM (devkitPro) with `3ds-dev` plus these portlibs:
`3ds-curl` (gpSP 3D's update check, Icon Maker's box art), `3ds-libjpeg-turbo`
and `3ds-libpng` (Icon Maker). Also `bannertool` and `makerom` on the PATH.

```
bash gpsp3ds/build-gpsp3ds.sh      # -> gpsp3ds/out/gpsp3d.cia, gpsp3d.3dsx
bash iconmaker/build-iconmaker.sh  # -> iconmaker/out/iconmaker.cia, iconmaker.3dsx
bash docker/ci-build.sh            # both + the forwarder test, as CI does
```

- The build image is `docker/gpsp3d-devcontainer.Dockerfile` (devkitARM pinned
  by digest). `docker/build-image.sh` names it by the Dockerfile's hash.
- `ci-build.sh` fails without libcurl on purpose: without it the update check
  falls back to the 3DS's own HTTPS, which can't reach GitHub.
- `OPT=-O3` builds the app code at -O3 (measured: no gain); `make TIMING=1`
  in `iconmaker/` builds a timing variant of Icon Maker.
- There's no devkitPro in most agent sandboxes. A syntax check with a plain
  `arm-none-eabi-gcc -fsyntax-only` against libctru/citro3d headers catches
  most mistakes; say clearly that it wasn't built or run on hardware.

## Versions and releases

- gpSP 3D's version is in two places that must agree with the release tag:
  `GPSP3D_VERSION` in `gpsp3ds/source/ui_screens.h` and `-major/-minor/-micro`
  in `gpsp3ds/build-gpsp3ds.sh`. `release.yml` refuses a tag that disagrees.
  Tags are `vX.Y.Z`, three numbers only (the CIA version can't hold four).
- Icon Maker has its own version (`ICONMAKER_VERSION` in
  `iconmaker/source/main.c` and `-micro` in `build-iconmaker.sh`), shipped in
  the same release. Bump its CIA version whenever it changes, or the update
  won't install over the old one.
- Publishing a GitHub release (not a draft saved first, "Set as the latest
  release" ticked) runs `release.yml`: it builds, uploads `gpsp3d.cia`,
  `gpsp3d.3dsx`, `iconmaker.cia`, `iconmaker.3dsx` and adds FBI QR codes to
  the notes. Keep those asset names: the store and `releases/latest/download`
  links depend on them. Snes9x 3D's assets are `snes9x_3ds.cia` and
  `snes9x_3ds_layers.3dsx`.
- Every push to `main` refreshes the **nightly** pre-release (CI).
- Title IDs (unique IDs): gpSP 3D `0xBAC81`, Icon Maker `0xF3D01`, forwarder
  template `0xF3D00` (Icon Maker gives each forwarder its own).

## 3DS rules learned the hard way

These all broke something on real hardware at least once.

**Old 3DS performance**
- 268 MHz ARM11, no hardware divide, small caches. Measure (`profiler.c`, the
  Performance bottom screen, `make TIMING=1`) before and after; report numbers.
- `osSetSpeedupEnable(true)` at startup (804 MHz + L2 on a New 3DS).
- **No SD card writes while a game runs.** Logs and profiler output stay in
  memory (`prof_defer_log`, `log_flush()`) and are written when the menu
  opens or the game closes. SD writes mid-game caused random freezes.
- Drawing runs on core 2 (`video_thread`); 3D draws each eye separately.

**System services**
- `gsp::Lcd` (bottom backlight): open it per call (`gspLcdInit`/`gspLcdExit`).
  Holding it open froze the 3DS when HOME was pressed.
- The 3DS's own HTTPS (`httpc`) can't talk to github.com (TLS too old). Use
  libcurl + mbedTLS, with `socInit` only for the request (`update.c`).
- Old 2DS and New 2DS XL have no 3D screen; the Old 2DS has no 800px mode
  (`CFGU_GetSystemModel`).

**HOME Menu banners** (`make_banner3d.py`, `iconmaker/source/banner.c`)
- A banner is a CBMD (0x88 header) + LZ11-compressed CGFX + CWAV (its offset
  at 0x84).
- Sound: **stereo PCM16 CWAV at 32 kHz**. Mono played as a 4.4 kHz beeping.
- CGFX that the real HOME Menu accepts (pycgfx output needs these, see
  `hardware_fixes()`): every mesh bound to its bone (`mesh_node_name`), no
  skinning, every material textured (a textureless one crashed HOME), matte
  materials, animations with translation, rotation and scale. Rounded
  outlines in the geometry and tiles without sides froze HOME when animated:
  the tile is a box, corners rounded by the label texture's alpha.
- SMDH flags `0x125` (visible, allow 3D, record usage, extended banner),
  at 0x2028. The HOME Menu caches banners per title and version.
- Icon Maker has two templates: 3D (`fwd_gpsp3d_3d.bin`) and flat
  (`fwd_gpsp3d.bin`). Both need the chime. A game's own `.banner` picture
  always uses the flat one.
- Reinstalling a forwarder with different contents at the same title and
  version fails with `C8E083FC` (AM: already exists); Icon Maker deletes the
  old title and retries.

**Touch UI**
- Menu taps act when the finger lifts without moving (dragging the list must
  not change settings); sliders follow the finger.
- Tabs size to their labels; check text widths with the real fonts (`ui_font.c`).

## How to work here

- **Commits**: author `bheppe <52939532+bheppe@users.noreply.github.com>`
  unless you are a contributor committing as yourself. A `Co-Authored-By:`
  line for the AI is fine; **no session links** (`Claude-Session:` etc.) in
  commit messages.
- **Small, testable steps.** The maintainer tests on an Old 3DS. Say exactly
  what to test, with exact menu paths (e.g. **System → Bottom screen →
  Performance**) and exact commands.
- **Writing style** (README, ROADMAP, release notes, UI text): plain, short
  sentences, exact setting names, no hype. UI strings must fit the 320x240
  bottom screen; help text is two lines.
- **Settings** live in `sdmc:/3ds/gpsp3d/gpsp3d.cfg` (`config_load` /
  `config_save` in `main.c`); per-game files in `sdmc:/3ds/gpsp3d/games/`
  (`<game>.cfg` 3D profile, `<game>.keys` buttons). Add new keys to both
  load and save, with safe defaults for old files.
- **ROADMAP.md**: add a row for every change (what, who asked, status
  🧪 testing / ✅ done / 📋 planned).
- **Credits**: name who asked for or reported something (Reddit/Discord/GitHub
  handle) in ROADMAP and release notes.
- **PRs**: describe what changed, measured before/after numbers where speed
  matters, what was tested on which hardware, and what wasn't.

## Licenses

gpSP and gpSP 3D: GPL-2.0-or-later (`COPYING`). `ctr_svchax.c` (from
RetroArch) is GPL-3.0, so the 3DS app binary as a whole is GPL-3.0. mGBA GPU
code: MPL-2.0. Inter font: OFL 1.1. Keep third-party notices when moving code.
