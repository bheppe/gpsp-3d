# gpSP 3D roadmap

What's been reported, what's done, and what's next. Reports come from
[Issues](../../issues), the Reddit thread and Discord. Status as of v0.1.4 (updated Oct 28).

**Status key:** ✅ done · 🧪 done, needs testing on real hardware · 🔧 next up · 📋 planned · 💤 later / big project

## Released

| Version | Change | Status |
|---|---|---|
| v0.1.0 | First release: per-layer 3D, Smart layers, presets, per-game profiles, GPU display, touch menu | ✅ |
| v0.1.1 | **Crash at launch on New 3DS** fixed (heap too big for the New 3DS memory layout) | ✅ confirmed by New 3DS users (#2, #3, closed) |
| v0.1.1 | **Menu reacts instantly**: the game preview redraws after you stop pressing | ✅ |
| v0.1.1 | **3D → Floor tilt**: bottom of the screen nearer, top further, for single-layer roads | ✅ |
| v0.1.1 | **Gyro sensor** for WarioWare: Twisted, from the 3DS gyroscope | 🧪 needs the game |
| v0.1.1 | **Forwarder support**: a ROM path as the launch argument plays that game directly | ✅ (with Icon Maker) |
| v0.1.1 | Ko-fi / GitHub Sponsors button, README Support section | ✅ |
| v0.1.1 | **Display → Screen effect**: Scanlines, LCD grid | 🧪 |
| v0.1.1 | **HOME Menu icons per game**: `make-forwarders.sh` builds one forwarder CIA per game | 🧪 |
| v0.1.1 | **Auto frame skip** goes down to 1 of 6 frames on very heavy 3D screens (Mario Kart menus), so the game and sound never slow down | ✅ |
| v0.1.2 | **Closing the lid no longer freezes the game** (it waited forever for sound the system had stopped for sleep) | ✅ |
| v0.1.2 | **Faster menus**: the in-game menu looked for state files on the SD card every frame; the game list scrolls faster when held | ✅ |
| v0.1.2 | **Sharp scaling at Fit/Stretch** keeps every pixel the same size | ✅ |
| v0.1.2 | **800px wide mode in 2D** (3D slider down) at Fit / Stretch; not on Old 2DS | 🧪 (#4, closed) |
| v0.1.2 | **OpenLara black screen** fixed: an interrupt during FIQ mode corrupted r8-r12 | 🧪 (#5, closed; reopen if OpenLara still shows a black screen) |
| v0.1.2 | Dynarec: writes over RAM code drop only the pages written to (Mario Golf) | ✅ |
| v0.1.3 | **Icon Maker**: HOME Menu icons for GBA games made on the 3DS, no PC; own or generated icon and banner, a whole folder at once, delete from the app | ✅ |
| v0.1.3 | **Icon Maker: SNES games too**, opened in Snes9x 3D (v1.61-layers6+); SNES cartridge icon, GBA/SNES tags in My icons; GBA + SNES app icon and banner | ✅ |
| v0.1.3 | `make-forwarders.sh`: custom banner per game (`<game>.banner.png`, optional `.banner.wav`) | 🧪 |
| v0.1.3 | Dynarec: old RAM translations reused when the same code comes back, writes drop only the blocks they cover, cheaper patched-routine handling (Mario Golf, Pokémon GS Chronicles) | 🧪 |
| v0.1.3 | Dynarec: code run from VRAM translated instead of interpreted, and it no longer flushes all RAM code (Super Mario Galaxy GBA port) | 🧪 |
| v0.1.3 | Dynarec (ARM): RAM/ROM loads done inline when the flags aren't needed; memory handlers return in a way the CPU can predict | 🧪 |
| v0.1.3 | Render thread: bitmap modes 3-5 copy each row with its line instead of VRAM blocks | 🧪 |
| v0.1.4 | **Fast forward** on Y (hold or toggle), Max / 2x / 3x / 4x | 🧪 |
| v0.1.4 | **Zipped games work** (#6: a .zip was loaded as if it were the ROM, which hung, then crashed): unzipped once to a cache on the SD card | 🧪 (#6, closed) |
| v0.1.4 | **System -> Save file** shows where the current game's save is | 🧪 |
| v0.1.4 | **Guide viewer**: a game's `.txt` guide on the bottom screen while playing (Game -> Show guide); drag or page to scroll, place remembered per game | 🧪 (cabjoy) |
| v0.1.4 | **Own save / state folder and `.sav` saves** (`save_dir`, `state_dir`, `save_ext` in `gpsp3d.cfg`) to share saves with mGBA and open_agb_firm; `.sav` files have the real save size, old saves are still found | 🧪 (michvllni, #7 / #8) |
| v0.1.4 | **Universal-Updater store** with gpSP 3D, Icon Maker and Snes9x 3D ([bheppe.github.io/gpsp-3d](https://bheppe.github.io/gpsp-3d/)); devcontainer, CI builds for every PR, Nightly pre-release from `main`, release workflow | ✅ (michvllni, #9-#12) |

## Next up

**gpSP 3D v0.1.5: requests and polish** (first release through the CI pipeline: test the Nightly, then publish)

| Change | Asked by | Status |
|---|---|---|
| **Tilt sensor** for Yoshi Topsy-Turvy and Koro Koro Puzzle Happy Panechu, from the 3DS accelerometer (like the gyro for WarioWare) | After-Pop-2000 (Reddit) | 🔧 |
| **New app icons** so gpSP 3D and Snes9x 3D are easy to tell apart on the HOME Menu | TheBigMacWhopperGuy (Reddit) | 🔧 |
| **More LCD filters**, plus your own PNG overlay from the SD card (the 3DS GPU has no pixel shaders, so RetroArch shaders can't run, but overlays can) | King_Hikikomori (Reddit) | 🔧 |
| **About screen** crediting gpSP's authors and contributors (was #15) | bheppe | 🔧 |

**Snes9x 3D v1.61-layers8: controls** ([snes9x_3ds](https://github.com/bheppe/snes9x_3ds), before v0.1.5)

| Change | Asked by | Status |
|---|---|---|
| **Hotkey combos** | cabjoy (Discord) | 🔧 |
| **Two depth profiles per game**, switched with a hotkey (e.g. overworld vs battle in EarthBound) | Finn (Discord) | 🔧 |
| **True 1:1 integer scale** | Finn (Discord) | 🔧 |
| **Guide viewer** on the bottom screen, like gpSP 3D's | cabjoy (Discord) | 🔧 |
| Donkey Kong Country 2: outline glitch | margret (Discord) | 📋 needs investigating |

**Later, each as its own release**

| Plan | Status |
|---|---|
| **v0.1.6: performance.** Mario Golf, the Super Mario Galaxy port, Golden Sun and WarioWare: Twisted on Old 3DS (#8, #10) likely share a cause: the dynarec's register handling | 📋 |
| **Icon Maker v0.2: box art download** when making an icon (ScreenScraper, SteamGridDB or RetroArch's libretro-thumbnails, which needs no account) | 📋 Gaming4LifeDE, Surge And Finn (Discord) |
| **Genesis 3D** | 💤 next big project |
| Screenshots and 3D GIFs in the READMEs | 📋 |
| Submit to Universal-DB (the main Universal-Updater store) | 📋 later |

## Open

| # | Problem or request | Reported by | Status |
|---|---|---|---|
| 6 | **New 3DS: use the extra CPU cores** for drawing instead of sharing core 2 with the system | — | 📋 |
| 7 | Need for Speed Underground: ~4 fps on the title screen, then freezes | myownfriend (#2) | 🔧 needs the game to reproduce |
| 8 | Stutter in Mario Golf: Advance Tour, Super Mario Galaxy (GBA port), NFS Underground; WarioWare: Twisted a little choppy on Old 3DS (smooth on New 3DS) | Discord, After-Pop-2000 (Reddit) | 🔧 Mario Golf: much better in v0.1.3 (RAM code rewrites cost about half), still slows down on the course view and the swing on Old 3DS. Galaxy port: 55% -> about 76% speed on Old 3DS; the rest needs a bigger dynarec change (register allocation), on hold. Pokémon GS Chronicles: fine |
| 9 | Classic NES Series: Castlevania doesn't boot | Male_Inkling | 📋 |
| 10 | Golden Sun: big frame drops that then settle (Old 3DS) | ClockIllustrious2077 | 📋 also Camelot: may have the same cause as Mario Golf (#8), try v0.1.3 |
| 11 | Doom II: 11 fps, no depth (one bitmap layer) | Male_Inkling | 📋 depth: try Floor tilt |
| 12 | Faster 3D drawing on Old 3DS (3D pictures refresh ~15-30/s) | bheppe | 📋 |
| 13 | RetroAchievements | MysticFlipFlop (#1, open) | 📋 casual mode first; hardcore needs 6 months of releases |
| 14 | Link cable / wireless adapter multiplayer, e.g. **Pokémon trading with RetroArch's gpSP** on a PC, phone or another 3DS | Han_Solo6712, Shot_Jicama4942 (Reddit), Discord | 📋 after Genesis 3D. The gpSP core already emulates the wireless adapter and link cable (packets over RetroArch netplay); gpSP 3D needs RetroArch's netplay protocol (packet mode) and a host/join menu |
| 15 | In-app About screen crediting gpSP's authors | bheppe | 🔧 in v0.1.5 |
| 16 | Per-layer 3D for other emulators (PS1 / PCSX) | _SleepyLark_, LargoLaGrande1, goldaxis | 💤 |
| 17 | Smashing Drive: 60-70% speed on Old 3DS (software 3D) | Advil Lavs (Discord) | 🔧 needs a profile log from a race |

Tip while waiting for fixes: if the stats panel says **Core 2: off (drawing on main core)**, turn on
**System → Draw on core 2**; drawing on the main core costs a lot of speed.

## Works well (community reports, Old 3DS)

Sonic Advance 1 and 2, Ace Combat Advance, Asterix, Downhill Jam, Pokémon Emerald
(and the ARC hack), Mario Kart Super Circuit, Sword of Mana.
