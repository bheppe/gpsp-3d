# gpSP 3D roadmap

What's been reported, what's done, and what's next. Reports come from
[Issues](../../issues), the Reddit thread and Discord. Status as of v0.1.1 (updated Oct 24).

**Status key:** ✅ done · 🧪 done, needs testing on real hardware · 🔧 next up · 📋 planned · 💤 later / big project

## Released

| Version | Change | Status |
|---|---|---|
| v0.1.0 | First release: per-layer 3D, Smart layers, presets, per-game profiles, GPU display, touch menu | ✅ |
| v0.1.1 | **Crash at launch on New 3DS** fixed (heap too big for the New 3DS memory layout) | ✅ confirmed by New 3DS users (#2, #3) |
| v0.1.1 | **Menu reacts instantly**: the game preview redraws after you stop pressing | ✅ |
| v0.1.1 | **3D → Floor tilt**: bottom of the screen nearer, top further, for single-layer roads | ✅ |
| v0.1.1 | **Gyro sensor** for WarioWare: Twisted, from the 3DS gyroscope | 🧪 needs the game |
| v0.1.1 | **Forwarder support**: a ROM path as the launch argument plays that game directly | 🧪 needs forwarder tools tested |
| v0.1.1 | Ko-fi / GitHub Sponsors button, README Support section | ✅ |
| v0.1.1 | **Display → Screen effect**: Scanlines, LCD grid | 🧪 |
| v0.1.1 | **HOME Menu icons per game**: `make-forwarders.sh` builds one forwarder CIA per game | 🧪 |
| v0.1.1 | **Auto frame skip** goes down to 1 of 6 frames on very heavy 3D screens (Mario Kart menus), so the game and sound never slow down | ✅ |

## Open

| # | Problem or request | Reported by | Status |
|---|---|---|---|
| 1 | **3DS freezes when the lid is closed** while the app runs | Discord | 🔧 v0.1.2 |
| 2 | **Menus feel laggy** (game list and in-game menu); possibly worse with big ROM folders | Cabjoy, Trvial, Discord | 🔧 v0.1.2 |
| 3 | **Sharp scaling at Fit/Stretch looks like bad transparency** (uneven pixels break up dithering) | Surge And Finn (Discord) | 🔧 v0.1.2; use Crisp meanwhile |
| 4 | **2D: use the top screen's 800px wide mode** for exact 3x horizontal pixels, sharper than now | myownfriend (#4) | 🔧 v0.1.2 |
| 5 | **OpenLara (GBA homebrew): music but black screen** (software-rendered bitmap mode) | myownfriend (#5) | 🔧 v0.1.2, open-source game, can be tested |
| 6 | **New 3DS: use the extra CPU cores** for drawing instead of sharing core 2 with the system | — | 📋 v0.1.2 |
| 7 | Need for Speed Underground: ~4 fps on the title screen, then freezes | myownfriend (#2) | 🔧 needs the game to reproduce |
| 8 | Stutter in Mario Golf: Advance Tour, "Galaxy" (?), NFS Underground | Discord | 📋 needs stats photos |
| 9 | Classic NES Series: Castlevania doesn't boot | Male_Inkling | 📋 |
| 10 | Golden Sun: big frame drops that then settle (Old 3DS) | ClockIllustrious2077 | 📋 |
| 11 | Doom II: 11 fps, no depth (one bitmap layer) | Male_Inkling | 📋 depth: try Floor tilt |
| 12 | Faster 3D drawing on Old 3DS (3D pictures refresh ~15-30/s) | bheppe | 📋 |
| 13 | RetroAchievements | MysticFlipFlop (#1) | 📋 casual mode first; hardcore needs 6 months of releases |
| 14 | Link cable / multiplayer between two 3DS consoles | Han_Solo6712, Discord | 💤 big project (local wireless between consoles) |
| 15 | In-app About screen crediting gpSP's authors | bheppe | 📋 v0.1.2 |
| 16 | Per-layer 3D for other emulators (PS1 / PCSX) | _SleepyLark_, LargoLaGrande1, goldaxis | 💤 |

Tip while waiting for fixes: if the stats panel says **Core 2: off (drawing on main core)**, turn on
**System → Draw on core 2**; drawing on the main core costs a lot of speed.

## Works well (community reports, Old 3DS)

Sonic Advance 1 and 2, Ace Combat Advance, Asterix, Downhill Jam, Pokémon Emerald
(and the ARC hack), Mario Kart Super Circuit, Sword of Mana.
