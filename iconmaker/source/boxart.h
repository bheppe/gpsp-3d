#ifndef BOXART_H
#define BOXART_H
#include <stddef.h>
#include <stdint.h>

/* Box art from libretro's thumbnails (libretro-thumbnails on GitHub: the
 * covers RetroArch shows) for a game found in libretro's No-Intro game
 * list (libretro-database, CC BY-SA 4.0) by its file name or CRC32; and
 * from box art libraries of your own laid out the same way. Plain C with
 * netget.c, libpng and zlib, so it also builds on a PC (test/boxarttest.c).
 *
 * sys: BOXART_GBA or BOXART_SNES (the same numbers as Icon Maker's
 * SYS_GBA, SYS_SNES). */
enum { BOXART_GBA, BOXART_SNES, BOXART_SYSTEMS };

enum { BOXART_SAVED, BOXART_NONE, BOXART_ERROR, BOXART_STOPPED };

/* A game list: CRC32 -> name, sorted by CRC32 */
typedef struct { uint32_t crc; const char *name; } boxart_game;
typedef struct { char *text; boxart_game *g; int n; } boxart_list;

/* Where box art comes from. base NULL: libretro. Else a library of your
 * own at base (a URL ending in /), with a folder per system named as
 * libretro-thumbnails' repositories:
 *   base Nintendo_-_Game_Boy_Advance/list.txt          (optional)
 *   base Nintendo_-_Game_Boy_Advance/Named_Boxarts/<name>.png
 * (and Nintendo_-_Super_Nintendo_Entertainment_System/). list.txt has a
 * line per game, "CRC32 name" (1A2B3C4D Golf (USA) [Hack by Me]); without
 * it, games are found by their file name. In a picture's file name,
 * & * / : ` < > ? \ | " in the name are _, as in libretro's. */
typedef struct { const char *base; boxart_list list; } boxart_source;

/* Reads the list of base's sys from cache (a text file of "CRC32 name"
 * lines), or, if there's none, downloads it (libretro's: ~1 MB) and
 * writes it there. net_begin() first. 0 (a library without a list.txt
 * gets an empty list, not cached), BOXART_STOPPED, or BOXART_ERROR with a
 * message in err. */
int boxart_list_load(boxart_list *l, const char *base, int sys, const char *cache, char *err, int errlen);
void boxart_list_free(boxart_list *l);

/* Box art for the game at path (a .gba, .sfc, ... or a .zip), from the
 * first of the n sources that has it, saved as a PNG at out, shrunk while
 * its shorter side stays at least side pixels. In each source the names
 * tried, best first: the game's own (by file name, or the CRC32: the file
 * is read whole once unless it's a zip), then the same title from other
 * regions. BOXART_SAVED (*found: the name whose picture was saved, *from:
 * the source's index), _NONE (no source has it), _STOPPED, or _ERROR (err:
 * the first source that failed; the others are still tried). */
int boxart_get(const boxart_source *src, int n, const char *path, int sys, const char *out, int side,
               char *found, int foundlen, int *from, char *err, int errlen);

/* If set: boxart_stop is called as the game is read and pictures come in
 * (nonzero: stop, BOXART_STOPPED); boxart_progress gets the step (below),
 * the bytes so far and the size (0 if unknown). */
enum { BOXART_STEP_LIST, BOXART_STEP_READ, BOXART_STEP_DOWNLOAD };
extern int (*boxart_stop)(void);
extern void (*boxart_progress)(int step, size_t done, size_t total);

#endif
