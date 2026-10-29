# gpSP 3D roadmap

What's been reported, what's done, and what's next. Reports come from
[Issues](../../issues), the Reddit thread and Discord. Status as of v0.1.4, with v0.1.5 in testing (updated Oct 29).

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

**gpSP 3D v0.1.5: fixes and quick wins** (first release through the CI pipeline: test the Nightly, then publish)

| Change | Asked by | Status |
|---|---|---|
| **Freezes on Old 3DS**: the log was written to the SD card while playing; now it's kept in memory and written only when the menu opens or the game closes | miguelcepoi (Discord) | 🧪 waiting on his test |
| **No SD card writes while playing** at all (log, as above; saves were already written only at the menu, sleep and exit) | bheppe | 🧪 |
| **System → Real-time clock**: Auto / On / Off, for Pokémon games whose clock the auto check misses | Individual_Emu673 (Reddit) | 🧪 |
| **System → Startup**: the GBA BIOS intro, when `gba_bios.bin` is in `sdmc:/3ds/gpsp3d/` | Individual_Emu673 (Reddit) | 🧪 |
| **Tilt sensor** for Yoshi Topsy-Turvy and Koro Koro Puzzle Happy Panechu, from the 3DS accelerometer | After-Pop-2000 (Reddit) | 🧪 needs the games |
| **System → Bottom screen**: Always on / Dim after 10 s / Off after 10 s; a tap wakes it, the guide keeps it on | PJ_MJ (Discord) | 🧪 |
| **Smoother guide scrolling** with flick (from Snes9x 3D) | cabjoy (Discord) | 🧪 |
| **System → Credits**: gpSP's authors and contributors | bheppe | 🧪 |
| **Icon Maker 0.1.4: accented letters** (Pokémon no longer shows "Pok??mon") | PJ_MJ (Discord) | 🧪 |
| **Icon Maker 0.1.4: banner style** (SELECT): blue background, no background, or **3D pop-out with a jingle** (the icon floats out of the screen and sways) | PJ_MJ (Discord), bheppe | 🧪 3D needs testing |
| **New app icons** so gpSP 3D and Snes9x 3D are easy to tell apart | TheBigMacWhopperGuy (Reddit) | 🔧 picking a design |
| `OPT=-O3` test build of the app code (the core is already -O3) | Gaming4LifeDE (Discord) | 🔧 measure |

**Snes9x 3D v1.61-layers8: controls** ([snes9x_3ds](https://github.com/bheppe/snes9x_3ds))

| Change | Asked by | Status |
|---|---|---|
| **Hotkey combos** | cabjoy (Discord) | ✅ |
| **True 1:1 integer scale** (No Stretch 1:1) | Finn (Discord) | ✅ |
| **Two depth profiles per game**, switched with a hotkey (e.g. overworld vs battle in EarthBound) | Finn (Discord) | 🧪 |
| **Guide viewer** on the bottom screen, like gpSP 3D's | cabjoy (Discord) | 🧪 |
| Donkey Kong Country 2: outline glitch | margret (Discord) | 📋 needs investigating |

**gpSP 3D v0.1.6: performance and updates**

| Plan | Status |
|---|---|
| **New 3DS: drawing on its own CPU core** instead of sharing core 2 with the system | 📋 |
| **Dynarec register allocation**: Mario Golf, the Super Mario Galaxy port, Golden Sun and WarioWare: Twisted on Old 3DS (#8, #10) | 📋 |
| **Update check in the app**: tells you when a new version is out (install it with Universal-Updater) | 📋 DukeOfJelly (Discord) |
| **More LCD filters**, plus your own PNG overlay from the SD card | 📋 King_Hikikomori (Reddit) |

**Icon Maker v0.2: box art**

| Plan | Status |
|---|---|
| **Box art download** from libretro-thumbnails (no account), matched by the ROM's CRC32, with a name search for other regions | 📋 Gaming4LifeDE, Surge, Finn (Discord) |
| Community art repo for original icons and banners (no copies of official box art) | 📋 |
| Ready-to-show copies of the thumbnails, so making a whole folder stays fast | 📋 |

**Later**

| Plan | Status |
|---|---|
| Tilt the layers with the 3DS (parallax from the gyroscope) | 💤 |
| DS game forwarders in Icon Maker (nds-bootstrap) | 💤 |
| Game library with RomM sync | 💤 Gaming4LifeDE |
| **Genesis 3D** | 💤 next big project |
| Screenshots and 3D GIFs in the READMEs | 📋 |
| Submit to Universal-DB (the main Universal-Updater store) | 📋 later |

## Open

| # | Problem or request | Reported by | Status |
|---|---|---|---|
| 6 | **New 3DS: use the extra CPU cores** for drawing instead of sharing core 2 with the system | — | 📋 v0.1.6 |
| 7 | Need for Speed Underground: ~4 fps on the title screen, then freezes | myownfriend (#2) | 🔧 needs the game to reproduce |
| 8 | Stutter in Mario Golf: Advance Tour, Super Mario Galaxy (GBA port), NFS Underground; WarioWare: Twisted a little choppy on Old 3DS (smooth on New 3DS) | Discord, After-Pop-2000 (Reddit) | 🔧 Mario Golf: much better in v0.1.3 (RAM code rewrites cost about half), still slows down on the course view and the swing on Old 3DS. Galaxy port: 55% -> about 76% speed on Old 3DS; the rest needs a bigger dynarec change (register allocation), on hold. Pokémon GS Chronicles: fine |
| 9 | Classic NES Series: Castlevania doesn't boot | Male_Inkling | 📋 |
| 10 | Golden Sun: big frame drops that then settle (Old 3DS) | ClockIllustrious2077 | 📋 also Camelot: may have the same cause as Mario Golf (#8), try v0.1.3 |
| 11 | Doom II: 11 fps, no depth (one bitmap layer) | Male_Inkling | 📋 depth: try Floor tilt |
| 12 | Faster 3D drawing on Old 3DS (3D pictures refresh ~15-30/s) | bheppe | 📋 |
| 13 | RetroAchievements | MysticFlipFlop (#1, open) | 📋 casual mode first; hardcore needs 6 months of releases |
| 14 | Link cable / wireless adapter multiplayer, e.g. **Pokémon trading with RetroArch's gpSP** on a PC, phone or another 3DS | Han_Solo6712, Shot_Jicama4942 (Reddit), Discord | 📋 after Genesis 3D. The gpSP core already emulates the wireless adapter and link cable (packets over RetroArch netplay); gpSP 3D needs RetroArch's netplay protocol (packet mode) and a host/join menu |
| 15 | In-app About screen crediting gpSP's authors | bheppe | 🧪 System → Credits in v0.1.5 |
| 16 | Per-layer 3D for other emulators (PS1 / PCSX) | _SleepyLark_, LargoLaGrande1, goldaxis | 💤 |
| 17 | Smashing Drive: 60-70% speed on Old 3DS (software 3D) | Advil Lavs (Discord) | 🔧 needs a profile log from a race |
| 18 | Random freezes on Old 3DS after a while | miguelcepoi (Discord) | 🧪 likely the SD log writes, fixed in v0.1.5 |

Tip while waiting for fixes: if the stats panel says **Core 2: off (drawing on main core)**, turn on
**System → Draw on core 2**; drawing on the main core costs a lot of speed.

## Works well (community reports, Old 3DS)

Sonic Advance 1 and 2, Ace Combat Advance, Asterix, Downhill Jam, Pokémon Emerald
(and the ARC hack), Mario Kart Super Circuit, Sword of Mana.
