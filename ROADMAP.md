# gpSP 3D roadmap

What's been reported, what's done, and what's next. Reports come from
[Issues](../../issues), the Reddit thread and Discord. Status as of v0.1.1.

**Status key:** ✅ done · 🧪 done, needs testing on real hardware · 🔧 next up · 📋 planned · 💤 later / big project

## Released

| Version | Change | Status |
|---|---|---|
| v0.1.0 | First release: per-layer 3D, Smart layers, presets, per-game profiles, GPU display, touch menu | ✅ |
| v0.1.1 | **Crash at launch on New 3DS** fixed (heap too big for the New 3DS memory layout) | 🧪 needs New 3DS testers |
| v0.1.1 | **Menu reacts instantly**: the game preview redraws after you stop pressing | ✅ |
| v0.1.1 | **3D → Floor tilt**: bottom of the screen nearer, top further, for single-layer roads | ✅ |
| v0.1.1 | **Gyro sensor** for WarioWare: Twisted, from the 3DS gyroscope | 🧪 needs the game |
| v0.1.1 | **Forwarder support**: a ROM path as the launch argument plays that game directly | 🧪 needs forwarder tools tested |
| v0.1.1 | Ko-fi / GitHub Sponsors button, README Support section | ✅ |

## Open

| # | Problem or request | Reported by | Status |
|---|---|---|---|
| 1 | Need for Speed Underground drops to ~4 fps on the title screen, then freezes (HOME stops responding) | myownfriend (#2) | 🔧 needs the game to reproduce |
| 2 | Classic NES Series: Castlevania doesn't boot | Male_Inkling | 🔧 |
| 3 | Golden Sun: big frame drops that then settle (Old 3DS) | ClockIllustrious2077 | 📋 |
| 4 | Some games below 60 fps on Old 3DS (which ones: pending) | Cabjoy | 📋 waiting for details |
| 5 | Doom II: 11 fps, no depth (one bitmap layer) | Male_Inkling | 📋 depth: try Floor tilt |
| 6 | Sega Rally, V-Rally 3 look flat | Male_Inkling | 🧪 try 3D → Floor tilt (v0.1.1) |
| 7 | Game list feels laggy on Old 3DS | Cabjoy | 📋 waiting for details |
| 8 | Original (1:1) screen size is small | ROMANREIGNS599 | ✅ use Display → Screen size → Fit + Scaling → Crisp |
| 9 | HOME Menu icons per game | Clean-Space7974, Mumpmander | 📋 forwarder groundwork in v0.1.1 |
| 10 | CRT / retro screen filters | ClockIllustrious2077 | 📋 |
| 11 | RetroAchievements | MysticFlipFlop (#1) | 💤 |
| 12 | Link cable between two 3DS consoles | Han_Solo6712 | 💤 |
| 13 | Faster 3D drawing on Old 3DS (3D pictures refresh ~30/s now) | — | 💤 |
| 14 | Per-layer 3D for other emulators (PS1 / PCSX) | _SleepyLark_, LargoLaGrande1, goldaxis | 💤 |

## Works well (community reports, Old 3DS)

Sonic Advance 1 and 2, Ace Combat Advance, Asterix, Downhill Jam, Pokémon Emerald
(and the ARC hack), Mario Kart Super Circuit, Sword of Mana.
