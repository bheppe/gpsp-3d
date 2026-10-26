# gpSP 3D roadmap

What's been reported, what's done, and what's next. Reports come from
[Issues](../../issues), the Reddit thread and Discord. Status as of v0.1.3 (updated Oct 26).

**Status key:** ✅ done · 🧪 done, needs testing on real hardware · 🔧 next up · 📋 planned · 💤 later / big project

## Released

| Version | Change | Status |
|---|---|---|
| v0.1.0 | First release: per-layer 3D, Smart layers, presets, per-game profiles, GPU display, touch menu | ✅ |
| v0.1.1 | **Crash at launch on New 3DS** fixed (heap too big for the New 3DS memory layout) | ✅ confirmed by New 3DS users (#2, #3) |
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
| v0.1.2 | **800px wide mode in 2D** (3D slider down) at Fit / Stretch; not on Old 2DS | 🧪 (#4) |
| v0.1.2 | **OpenLara black screen** fixed: an interrupt during FIQ mode corrupted r8-r12 | 🧪 (#5) |
| v0.1.2 | Dynarec: writes over RAM code drop only the pages written to (Mario Golf) | ✅ |
| v0.1.3 | **Icon Maker**: HOME Menu icons for GBA games made on the 3DS, no PC; own or generated icon and banner, a whole folder at once, delete from the app | ✅ |
| v0.1.3 | **Icon Maker: SNES games too**, opened in Snes9x 3D (v1.61-layers6+); SNES cartridge icon, GBA/SNES tags in My icons; GBA + SNES app icon and banner | ✅ |
| v0.1.3 | `make-forwarders.sh`: custom banner per game (`<game>.banner.png`, optional `.banner.wav`) | 🧪 |
| v0.1.3 | Dynarec: old RAM translations reused when the same code comes back, writes drop only the blocks they cover, cheaper patched-routine handling (Mario Golf, Pokémon GS Chronicles) | 🧪 |
| v0.1.3 | Dynarec: code run from VRAM translated instead of interpreted, and it no longer flushes all RAM code (Super Mario Galaxy GBA port) | 🧪 |
| v0.1.3 | Dynarec (ARM): RAM/ROM loads done inline when the flags aren't needed; memory handlers return in a way the CPU can predict | 🧪 |
| v0.1.3 | Render thread: bitmap modes 3-5 copy each row with its line instead of VRAM blocks | 🧪 |

## Open

| # | Problem or request | Reported by | Status |
|---|---|---|---|
| 6 | **New 3DS: use the extra CPU cores** for drawing instead of sharing core 2 with the system | — | 📋 |
| 7 | Need for Speed Underground: ~4 fps on the title screen, then freezes | myownfriend (#2) | 🔧 needs the game to reproduce |
| 8 | Stutter in Mario Golf: Advance Tour, Super Mario Galaxy (GBA port), NFS Underground | Discord | 🔧 Mario Golf: much better in v0.1.3 (RAM code rewrites cost about half), still slows down on the course view and the swing on Old 3DS. Galaxy port: 55% -> about 76% speed on Old 3DS; the rest needs a bigger dynarec change (register allocation), on hold. Pokémon GS Chronicles: fine |
| 9 | Classic NES Series: Castlevania doesn't boot | Male_Inkling | 📋 |
| 10 | Golden Sun: big frame drops that then settle (Old 3DS) | ClockIllustrious2077 | 📋 also Camelot: may have the same cause as Mario Golf (#8), try v0.1.3 |
| 11 | Doom II: 11 fps, no depth (one bitmap layer) | Male_Inkling | 📋 depth: try Floor tilt |
| 12 | Faster 3D drawing on Old 3DS (3D pictures refresh ~15-30/s) | bheppe | 📋 |
| 13 | RetroAchievements | MysticFlipFlop (#1) | 📋 casual mode first; hardcore needs 6 months of releases |
| 14 | Link cable / multiplayer between two 3DS consoles | Han_Solo6712, Discord | 💤 big project (local wireless between consoles) |
| 15 | In-app About screen crediting gpSP's authors | bheppe | 📋 |
| 16 | Per-layer 3D for other emulators (PS1 / PCSX) | _SleepyLark_, LargoLaGrande1, goldaxis | 💤 |

Tip while waiting for fixes: if the stats panel says **Core 2: off (drawing on main core)**, turn on
**System → Draw on core 2**; drawing on the main core costs a lot of speed.

## Works well (community reports, Old 3DS)

Sonic Advance 1 and 2, Ace Combat Advance, Asterix, Downhill Jam, Pokémon Emerald
(and the ARC hack), Mario Kart Super Circuit, Sword of Mana.
