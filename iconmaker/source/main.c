/*
 * Icon Maker: puts GBA and SNES games on the HOME Menu, right from the 3DS.
 *
 * Pick a game; the app builds a small forwarder CIA for it (from the
 * template built into it, see fwdcia.h) and installs it. Started from the
 * HOME Menu, the forwarder opens the game's emulator straight into it:
 * gpSP 3D for GBA games, Snes9x 3D for SNES games (it writes the game's
 * path to the file the emulator reads at startup, then starts it).
 *
 * Name: the game's file name. Icon: a PNG or JPEG next to the game with
 * the same name (Golf.gba -> Golf.png or Golf.jpg, any size), else a
 * cartridge with the game's name on it. Top screen banner: Golf.banner.png
 * or .jpg (best 256x128), else the icon, bigger, with the name. A picture of
 * another shape is padded (the default) or cropped to fit (X on the Games
 * tab). The top screen shows both before anything is made.
 *
 * Y makes icons for every game in a folder. The "My icons" tab lists the
 * icons made (here or by make-forwarders.sh) and deletes them.
 *
 * The look (and ui.c, ui_font.c) is gpSP 3D's.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include "fwdcia.h"
#include "deficon.h"
#include "ui.h"
#include "pngshrink.h"

#include <turbojpeg.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO_WRITE
#include "stb_image.h"

/* the template forwarder CIAs (data/, built by build-iconmaker.sh): with a
 * flat banner, and with the 3D one (and its jingle) */
#include "fwd_gpsp3d_bin.h"
#include "fwd_gpsp3d_3d_bin.h"

#define ICONMAKER_VERSION "0.1.4"
#define MAX_ENTRIES 1024
#define MAX_ICONS   300
#define LAST_DIR    "sdmc:/3ds/gpsp3d/iconmaker_dir.txt"
#define SETTINGS    "sdmc:/3ds/gpsp3d/iconmaker.cfg"

/* The banner a game gets: on gpSP 3D's blue, on nothing (the HOME Menu's
 * own background shows behind the icon and the name), or 3D: the icon on a
 * tile that comes out of the screen and sways, over the blue, with a jingle
 * (SELECT on the Games tab switches; saved in iconmaker.cfg) */
enum { STYLE_BLUE, STYLE_CLEAR, STYLE_3D, STYLE_COUNT };
static const char *style_key[STYLE_COUNT] = { "blue", "none", "3d" };
static int banner_style = STYLE_3D;

/* A picture of another shape than the icon, banner or tile: shrunk to fit
 * with margins (white on the icon, as the HOME Menu draws see-through
 * parts; the tile's grey on the 3D banner), or
 * scaled to fill it with the sides cut off (X on the Games tab; saved too) */
enum { FIT_PAD, FIT_CROP, FIT_COUNT };
static const char *fit_key[FIT_COUNT] = { "pad", "crop" };
static int fit_mode = FIT_PAD;

/* pic[0], pic[1]: the game's icon and banner picture, as an index into
 * pic_types, or -1 if there's none (found when the folder is read) */
typedef struct { char name[256]; bool dir, has_icon; int sys; s8 pic[2]; } entry;
static const char *const pic_types[] = { "png", "jpg", "jpeg" };
/* Pictures too big to use (an icon from the name instead, and a note): a
 * side over PIC_MAX_SIDE, a file over PIC_MAX_FILE (JPEGs are read whole),
 * or, where a picture must be decoded all at once (interlaced PNGs, JPEGs
 * libjpeg-turbo can't read), over PIC_MAX_FULL bytes decoded */
#define PIC_MAX_SIDE  4096
#define PIC_MAX_FILE  (32 << 20)
#define PIC_MAX_FULL  (8 << 20)
typedef struct { u64 tid; char name[64]; u16 icon24[24 * 24]; int sys; } icon_entry;

/* the emulators an icon can start (the publisher on an icon names it) */
typedef struct
{
   const char *name, *console;
   u64 tid;                    /* its CIA's title ID */
   const char *launch;         /* the file it reads the game path from ("": gpSP 3D's) */
   int style;                  /* default icon: which cartridge */
   u32 tag_rgb;                /* its name's colour on the banner */
} emulator;
enum { SYS_GBA, SYS_SNES, SYS_COUNT };
static const emulator systems[SYS_COUNT] = {
   { "gpSP 3D",   "GBA",  0x000400000BAC8100ULL, "",                                ICON_GBA,  0x23D5C8 },
   { "Snes9x 3D", "SNES", 0x00040000038A7100ULL, "sdmc:/3ds/snes9x_3ds/launch.txt", ICON_SNES, 0xB59CFF },
};
static bool sys_installed[SYS_COUNT];

static entry entries[MAX_ENTRIES];
static int nentries, ngames;
static icon_entry icons[MAX_ICONS];
static int nicons;
static char cwd[512] = "sdmc:/";

/* the preview being made: when it started, and the spinner shown over the
 * top screen while a slow one is made (see preview_stop) */
static u64 preview_t0, spinner_last, spinner_ticks;

/* ------------------------------------------------------------------ */
/* timing (make TIMING=1): how long each step of the preview takes,    */
/* shown on the top screen and added to iconmaker_timing.log           */
/* ------------------------------------------------------------------ */

#ifdef ICONMAKER_TIMING
#define TIMING_LOG "sdmc:/3ds/gpsp3d/iconmaker_timing.log"
#define TM_MAX 20
static struct { const char *what; u32 us; int n; } tm[TM_MAX];
static int ntm;
static u64 tm_t0;
static char tm_pic[2][64];          /* the icon's and banner's file: size, bytes */
static u32 tm_dir_us, tm_dir_n, tm_bottom_us;

static u32 tm_us(u64 ticks)
{
   return (u32)(ticks / (SYSCLOCK_ARM11 / 1000000));
}

static void tm_start(void)
{
   ntm = 0;
   tm_pic[0][0] = tm_pic[1][0] = 0;
   tm_t0 = svcGetSystemTick();
}

/* the time since the last mark goes to this step (added up if it repeats) */
static void tm_mark(const char *what)
{
   u32 us = tm_us(svcGetSystemTick() - tm_t0);
   int i;
   for (i = 0; i < ntm && tm[i].what != what; i++)
      ;
   if (i < TM_MAX)
   {
      if (i == ntm)
      {
         tm[ntm].what = what;
         tm[ntm].us = tm[ntm].n = 0;
         ntm++;
      }
      tm[i].us += us;
      tm[i].n++;
   }
   tm_t0 = svcGetSystemTick();
}

static void tm_log(const char *line);

/* a session's first lines: when, and on which console */
static void tm_log_start(void)
{
   char line[160];
   time_t now = time(NULL);
   bool n3ds = false;
   APT_CheckNew3DS(&n3ds);
   strftime(line, 40, "\n=== %Y-%m-%d %H:%M:%S", localtime(&now));
   snprintf(line + strlen(line), sizeof(line) - strlen(line),
            "  Icon Maker " ICONMAKER_VERSION ", %s\n", n3ds ? "New 3DS" : "Old 3DS");
   tm_log(line);
}

/* a part of the step just marked, measured inside it (not added to the total) */
static void tm_note(const char *what, u64 ticks)
{
   if (ntm < TM_MAX)
   {
      tm[ntm].what = what;
      tm[ntm].us = tm_us(ticks);
      tm[ntm].n = -1;
      ntm++;
   }
}

static void tm_log(const char *line)
{
   FILE *f;
   mkdir("sdmc:/3ds", 0777);
   mkdir("sdmc:/3ds/gpsp3d", 0777);
   if ((f = fopen(TIMING_LOG, "a")))
   {
      fputs(line, f);
      fclose(f);
   }
}
#else
#define tm_start()  ((void)0)
#define tm_mark(w)  ((void)(w))
#endif

/* ------------------------------------------------------------------ */
/* theme (gpSP 3D's)                                                   */
/* ------------------------------------------------------------------ */

#define C_BG      UI_RGB(0x0E, 0x11, 0x17)
#define C_BG2     UI_RGB(0x16, 0x1A, 0x23)
#define C_PANEL   UI_RGB(0x1B, 0x20, 0x2A)
#define C_ROW     UI_RGB(0x20, 0x26, 0x31)
#define C_ROW_SEL UI_RGB(0x2A, 0x33, 0x45)
#define C_ACCENT  UI_RGB(0x4E, 0x8D, 0xFF)
#define C_ACCENT2 UI_RGB(0x8F, 0x6B, 0xFF)
#define C_ORANGE  UI_RGB(0xFF, 0xAA, 0x28)
#define C_TEXT    UI_RGB(0xEC, 0xEF, 0xF4)
#define C_TEXT2   UI_RGB(0xC5, 0xCB, 0xD6)
#define C_DIM     UI_RGB(0x8B, 0x94, 0xA7)
#define C_FAINT   UI_RGB(0x4A, 0x52, 0x63)
#define C_TRACK   UI_RGB(0x37, 0x3F, 0x50)
#define C_GOOD    UI_RGB(0x5F, 0xD3, 0x93)
#define C_BAD     UI_RGB(0xFF, 0x6E, 0x6E)
#define C_WARN    UI_RGB(0xFF, 0xC1, 0x5C)

#define ROW_X     6
#define ROW_W     (UI_W - 12)
#define TAB_Y     30
#define TAB_H     26
#define LIST_Y    76
#define ROW_H     23
#define ROWS      6
#define FOOT_Y    216

enum { TAB_GAMES, TAB_ICONS };

static void sha256_3ds(const void *data, u32 size, u8 out[32])
{
   FSUSER_UpdateSha256Context(data, size, out);
}

/* SMDH icon (RGB565, 8x8 tiles, Z-order) to a plain RGB565 picture */
static void smdh_icon_decode(const u8 *src, int size, u16 *out)
{
   int x, y, i = 0;
   for (y = 0; y < size; y += 8)
      for (x = 0; x < size; x += 8)
      {
         int k;
         for (k = 0; k < 64; k++, i++)
         {
            int px = 0, py = 0, b;
            for (b = 0; b < 3; b++)
            {
               px |= ((k >> (2 * b)) & 1) << b;
               py |= ((k >> (2 * b + 1)) & 1) << b;
            }
            out[(y + py) * size + x + px] = src[2 * i] | src[2 * i + 1] << 8;
         }
      }
}

/* ------------------------------------------------------------------ */
/* the icons on the HOME Menu (forwarders to gpSP 3D)                  */
/* ------------------------------------------------------------------ */

/* a title's SMDH, read from its ExeFS icon */
static bool read_title_smdh(u64 tid, u8 *smdh)
{
   static const u32 file_path[5] = { 0, 0, 2, 0x6E6F6369 /* "icon" */, 0 };
   u32 arch_path[4] = { (u32)tid, (u32)(tid >> 32), MEDIATYPE_SD, 0 };
   FS_Path ap = { PATH_BINARY, sizeof(arch_path), arch_path };
   FS_Path fp = { PATH_BINARY, sizeof(file_path), file_path };
   Handle h;
   u32 got = 0;
   if (R_FAILED(FSUSER_OpenFileDirectly(&h, ARCHIVE_SAVEDATA_AND_CONTENT, ap, fp, FS_OPEN_READ, 0)))
      return false;
   FSFILE_Read(h, &got, 0, smdh, FWDCIA_SMDH_SIZE);
   FSFILE_Close(h);
   return got == FWDCIA_SMDH_SIZE && !memcmp(smdh, "SMDH", 4);
}

static void utf16_to_ascii(const u8 *in, int max, char *out, int outlen)
{
   int i, k = 0;
   for (i = 0; i < max && k < outlen - 1; i++)
   {
      u16 c = in[2 * i] | in[2 * i + 1] << 8;
      if (!c)
         break;
      out[k++] = c >= 32 && c < 127 ? (char)c : '?';
   }
   out[k] = 0;
}

static int cmp_icon(const void *a, const void *b)
{
   return strcasecmp(((const icon_entry*)a)->name, ((const icon_entry*)b)->name);
}

/* Icons made by Icon Maker or make-forwarders.sh: titles 00040000 000C0000
 * to 000EFFxx whose publisher is one of the emulators' names. Also notes
 * which emulators are installed. */
static void read_icons(void)
{
   static u8 smdh[FWDCIA_SMDH_SIZE];
   u32 count = 0, got = 0, i;
   u64 *ids;
   nicons = 0;
   memset(sys_installed, 0, sizeof(sys_installed));
   if (R_FAILED(AM_GetTitleCount(MEDIATYPE_SD, &count)) || !count)
      return;
   ids = malloc(count * sizeof(u64));
   if (!ids)
      return;
   if (R_SUCCEEDED(AM_GetTitleList(&got, MEDIATYPE_SD, count, ids)))
      for (i = 0; i < got && nicons < MAX_ICONS; i++)
      {
         u32 uid = (u32)ids[i] >> 8;
         char pub[64];
         int k, sys = -1;
         for (k = 0; k < SYS_COUNT; k++)
            if (ids[i] == systems[k].tid)
               sys_installed[k] = true;
         if ((ids[i] >> 32) != 0x00040000 || uid < 0xC0000 || uid >= 0xF0000)
            continue;
         if (!read_title_smdh(ids[i], smdh))
            continue;
         utf16_to_ascii(smdh + 8 + 0x180, 0x40, pub, sizeof(pub));   /* English publisher */
         for (k = 0; k < SYS_COUNT; k++)
            if (!strcmp(pub, systems[k].name))
               sys = k;
         if (sys < 0)
            continue;
         icons[nicons].tid = ids[i];
         icons[nicons].sys = sys;
         utf16_to_ascii(smdh + 8, 0x40, icons[nicons].name, sizeof(icons[0].name));
         smdh_icon_decode(smdh + 0x2040, 24, icons[nicons].icon24);
         nicons++;
      }
   free(ids);
   qsort(icons, nicons, sizeof(icon_entry), cmp_icon);
}

static bool icon_installed(u64 tid)
{
   int i;
   for (i = 0; i < nicons; i++)
      if (icons[i].tid == tid)
         return true;
   return false;
}

/* ------------------------------------------------------------------ */
/* the file list                                                       */
/* ------------------------------------------------------------------ */

static int system_of_ext(const char *dot)
{
   if (!dot)
      return -1;
   if (!strcasecmp(dot, ".gba"))
      return SYS_GBA;
   if (!strcasecmp(dot, ".sfc") || !strcasecmp(dot, ".smc") ||
       !strcasecmp(dot, ".fig") || !strcasecmp(dot, ".swc"))
      return SYS_SNES;
   return -1;
}

/* Which emulator a game file is for, or -1 if it isn't a game. A .zip is
 * for the system of the first file in it (GBA if that can't be told). */
static int system_of(const char *path)
{
   const char *dot = strrchr(path, '.');
   int sys = system_of_ext(dot);
   if (sys >= 0 || !dot || strcasecmp(dot, ".zip"))
      return sys;
   {
      u8 hdr[30];
      char inner[256];
      FILE *f = fopen(path, "rb");
      int n;
      sys = SYS_GBA;
      if (!f)
         return sys;
      if (fread(hdr, 1, 30, f) == 30 && hdr[0] == 'P' && hdr[1] == 'K' && hdr[2] == 3 && hdr[3] == 4)
      {
         n = hdr[26] | hdr[27] << 8;
         if (n > 255) n = 255;
         if (fread(inner, 1, n, f) == (size_t)n)
         {
            inner[n] = 0;
            if (system_of_ext(strrchr(inner, '.')) == SYS_SNES)
               sys = SYS_SNES;
         }
      }
      fclose(f);
   }
   return sys;
}

/* The pictures in the folder, by name: looking for a file that isn't there
 * costs about 60 ms on the SD card, so each game's are found once, here */
static char **pics;
static int npics, pics_room;

static bool is_picture(const char *name)
{
   const char *dot = strrchr(name, '.');
   int i;
   for (i = 0; dot && i < 3; i++)
      if (!strcasecmp(dot + 1, pic_types[i]))
         return true;
   return false;
}

static void add_picture(const char *name)
{
   if (npics == pics_room)
   {
      int room = pics_room ? pics_room * 2 : 256;
      char **p = realloc(pics, room * sizeof(char*));
      if (!p)
         return;
      pics = p;
      pics_room = room;
   }
   if ((pics[npics] = strdup(name)))
      npics++;
}

static int cmp_name(const void *a, const void *b)
{
   return strcasecmp(*(char *const*)a, *(char *const*)b);
}

/* each game's icon (Golf.png) and banner picture (Golf.banner.png), if any */
static void find_pictures(void)
{
   int i, k, t;
   qsort(pics, npics, sizeof(char*), cmp_name);
   for (i = 0; i < nentries; i++)
   {
      const char *name = entries[i].name, *dot = strrchr(name, '.');
      entries[i].pic[0] = entries[i].pic[1] = -1;
      if (entries[i].dir || !dot)
         continue;
      for (k = 0; k < 2; k++)
         for (t = 2; t >= 0; t--)          /* png wins, as when they were opened in turn */
         {
            char want[300];
            const char *key = want;
            snprintf(want, sizeof(want), "%.*s%s.%s", (int)(dot - name), name, k ? ".banner" : "", pic_types[t]);
            if (bsearch(&key, pics, npics, sizeof(char*), cmp_name))
               entries[i].pic[k] = (s8)t;
         }
   }
   for (i = 0; i < npics; i++)
      free(pics[i]);
   npics = 0;
}

static int cmp_entry(const void *a, const void *b)
{
   const entry *x = a, *y = b;
   if (x->dir != y->dir)
      return x->dir ? -1 : 1;
   return strcasecmp(x->name, y->name);
}

static void read_dir(void)
{
#ifdef ICONMAKER_TIMING
   u64 t_dir = svcGetSystemTick();
#endif
   DIR *d = opendir(cwd);
   struct dirent *e;
   nentries = ngames = 0;
   if (strcmp(cwd, "sdmc:/"))
   {
      strcpy(entries[0].name, "..");
      entries[0].dir = true;
      nentries = 1;
   }
   if (!d)
      return;
   npics = 0;
   while ((e = readdir(d)) && nentries < MAX_ENTRIES)
   {
      bool dir = e->d_type == DT_DIR;
      char path[800];
      int sys = -1;
      if (e->d_name[0] == '.')
         continue;
      if (!dir && is_picture(e->d_name))
      {
         add_picture(e->d_name);
         continue;
      }
      snprintf(path, sizeof(path), "%s%s", cwd, e->d_name);
      if (!dir && (sys = system_of(path)) < 0)
         continue;
      snprintf(entries[nentries].name, sizeof(entries[0].name), "%s", e->d_name);
      entries[nentries].dir = dir;
      entries[nentries].sys = sys;
      entries[nentries].has_icon = false;
      if (!dir)
      {
         entries[nentries].has_icon = icon_installed(fwdcia_title_id_for(path));
         ngames++;
      }
      nentries++;
   }
   closedir(d);
   find_pictures();
   qsort(entries + (strcmp(cwd, "sdmc:/") ? 1 : 0),
         nentries - (strcmp(cwd, "sdmc:/") ? 1 : 0), sizeof(entry), cmp_entry);
#ifdef ICONMAKER_TIMING
   {
      char line[700];
      tm_dir_us = tm_us(svcGetSystemTick() - t_dir);
      tm_dir_n = nentries;
      snprintf(line, sizeof(line), "folder %s: %d entries, %d games, read in %.1f ms\n",
               cwd, nentries, ngames, tm_dir_us / 1000.0);
      tm_log(line);
   }
#endif
}

static void cache_clear(void);

static void enter(const char *name)
{
   size_t n = strlen(cwd);
   if (!strcmp(name, ".."))
   {
      if (n > 6)
      {
         cwd[n - 1] = 0;                       /* drop the trailing '/' */
         *(strrchr(cwd, '/') + 1) = 0;
      }
   }
   else if (n + strlen(name) + 2 < sizeof(cwd))
   {
      strcat(cwd, name);
      strcat(cwd, "/");
   }
   cache_clear();
   read_dir();
}

/* the template's SMDH (the ExeFS icon is the first one in the CIA) */
static const u8 *template_smdh(const u8 *cia, u32 size)
{
   u32 i;
   for (i = 0; i + FWDCIA_SMDH_SIZE <= size; i += 4)
      if (!memcmp(cia + i, "SMDH", 4))
         return cia + i;
   return NULL;
}

/* A JPEG at the smallest size libjpeg-turbo decodes it to (1/8, 1/4, ...)
 * that is still at least min_w x min_h, as RGBA; NULL if it can't read it.
 * Much faster than decoding all of a big cover and scaling it down after. */
static u8 *jpeg_load(const u8 *buf, size_t size, int min_w, int min_h, int *w, int *h, bool *too_big)
{
   static tjhandle tj;
   tjscalingfactor *sf, best = { 1, 1 };
   int nsf, i, jw, jh;
   u8 *img;
   if (!tj && !(tj = tj3Init(TJINIT_DECOMPRESS)))
      return NULL;
   if (tj3DecompressHeader(tj, buf, size))
      return NULL;
   jw = tj3Get(tj, TJPARAM_JPEGWIDTH);
   jh = tj3Get(tj, TJPARAM_JPEGHEIGHT);
   if (jw <= 0 || jh <= 0)
      return NULL;
   if (jw > PIC_MAX_SIDE || jh > PIC_MAX_SIDE)
   {
      *w = jw;
      *h = jh;
      *too_big = true;
      return NULL;
   }
   sf = tj3GetScalingFactors(&nsf);
   for (i = 0; sf && i < nsf; i++)
      if (sf[i].num <= sf[i].denom && TJSCALED(jw, sf[i]) >= min_w && TJSCALED(jh, sf[i]) >= min_h &&
          sf[i].num * best.denom < best.num * sf[i].denom)
         best = sf[i];
   if (tj3SetScalingFactor(tj, best))
      return NULL;
   *w = TJSCALED(jw, best);
   *h = TJSCALED(jh, best);
   if (!(img = malloc((size_t)*w * *h * 4)))
      return NULL;
   /* a warning (a damaged end, say) still gives a picture */
   if (tj3Decompress8(tj, buf, size, img, 0, TJPF_RGBA) && tj3GetErrorCode(tj) == TJERR_FATAL)
   {
      free(img);
      return NULL;
   }
   return img;
}

/* The game's picture with this ending ("" or ".banner") and type (an index
 * into pic_types, -1: none), as RGBA, or NULL. It comes at least
 * min_w x min_h (or its own size if smaller), but often not much bigger:
 * PNGs are shrunk while read (pngshrink.c), JPEGs decoded small by
 * libjpeg-turbo; stb_image decodes all of the rest (interlaced PNGs, JPEGs
 * libjpeg-turbo can't read) if that fits in PIC_MAX_FULL. NULL with
 * *too_big if the picture is too big for that, *w x *h its size (if known). */
static u8 *load_picture(const char *game, const char *ext, int type, int min_w, int min_h,
                        int *w, int *h, bool *too_big)
{
   char file[600];
   const char *dot = strrchr(game, '.');
   bool banner = ext[0] != 0;
   u8 sig[8], *buf = NULL, *img = NULL;
   long size;
   int n, interlaced = 0;
   u32 mem = 0;
   FILE *f;
   *too_big = false;
   *w = *h = 0;
   if (type < 0)
      return NULL;
   snprintf(file, sizeof(file), "%.*s%s.%s", (int)(dot - game), game, ext, pic_types[type]);
   if (!(f = fopen(file, "rb")))
      return NULL;
   setvbuf(f, NULL, _IOFBF, 64 * 1024);  /* the SD card is much faster in big reads */
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);
   /* by what's in it, not the name: a .jpg can be a PNG */
   if (fread(sig, 1, 8, f) == 8 && !memcmp(sig, "\x89PNG\r\n\x1a\n", 8))
   {
      fseek(f, 0, SEEK_SET);
      img = png_shrink(f, min_w, min_h, PIC_MAX_SIDE, w, h, &interlaced, &mem);
      tm_mark(banner ? "banner: read + shrink PNG (libpng)" : "icon: read + shrink PNG (libpng)");
#ifdef ICONMAKER_TIMING
      /* in that: the SD card, libpng + zlib unpacking, our averaging */
      tm_note("  of it: reading the SD card", png_shrink_read_ticks);
      tm_note("  of it: libpng + zlib", png_shrink_rows_ticks - png_shrink_read_ticks);
      tm_note("  of it: averaging rows", png_shrink_average_ticks);
      if (spinner_ticks)
         tm_note("  of it: drawing the spinner", spinner_ticks);
#endif
      if (!img && (!interlaced || *w > PIC_MAX_SIDE || *h > PIC_MAX_SIDE ||
                   (long long)*w * *h * 4 > PIC_MAX_FULL))
      {
         *too_big = *w > PIC_MAX_SIDE || *h > PIC_MAX_SIDE || interlaced;
         fclose(f);
         return NULL;
      }
   }
   if (!img)
   {
      buf = size > 0 && size <= PIC_MAX_FILE ? malloc(size) : NULL;
      fseek(f, 0, SEEK_SET);
      if (buf && fread(buf, 1, size, f) != (size_t)size)
      {
         free(buf);
         buf = NULL;
      }
      *too_big = size > PIC_MAX_FILE;
      tm_mark(banner ? "banner: read the file" : "icon: read the file");
   }
   fclose(f);
   if (buf && size > 2 && buf[0] == 0xFF && buf[1] == 0xD8)
   {
      img = jpeg_load(buf, size, min_w, min_h, w, h, too_big);
      tm_mark(banner ? "banner: decode (libjpeg-turbo)" : "icon: decode (libjpeg-turbo)");
   }
   if (buf && !img && !*too_big)
   {
      /* all of it at once: only if that fits */
      int iw, ih;
      if (stbi_info_from_memory(buf, size, &iw, &ih, &n))
      {
         if (iw > PIC_MAX_SIDE || ih > PIC_MAX_SIDE || (long long)iw * ih * 4 > PIC_MAX_FULL)
         {
            *w = iw;
            *h = ih;
            *too_big = true;
         }
         else
         {
            img = stbi_load_from_memory(buf, size, w, h, &n, 4);
            mem = (u32)iw * ih * 4;
         }
      }
      tm_mark(banner ? "banner: decode (stb_image)" : "icon: decode (stb_image)");
   }
#ifdef ICONMAKER_TIMING
   if (*too_big)
      snprintf(tm_pic[banner], sizeof(tm_pic[0]), "%s, %ld KB, %dx%d: too big%s",
               pic_types[type], size / 1024, *w, *h, interlaced ? " (interlaced)" : "");
   else
      snprintf(tm_pic[banner], sizeof(tm_pic[0]), "%s, %ld KB, decoded at %dx%d, %lu KB used",
               pic_types[type], size / 1024, img ? *w : 0, img ? *h : 0, (unsigned long)(mem / 1024));
#endif
   free(buf);
   return img;
}

/* a picture to ow x oh, padded or cropped (fit_mode) */
static void fit_picture(const u8 *img, int w, int h, u8 *out, int ow, int oh)
{
   if (fit_mode == FIT_CROP)
      image_fill(img, w, h, out, ow, oh);
   else
      image_fit(img, w, h, out, ow, oh);
}

/* Text in one of gpSP 3D's fonts onto an RGBA picture (w wide), with a
 * soft shadow. Returns the x after it. */
static int rgba_text(u8 *img, int w, int h, const ui_font *f, int x, int y, const char *s, u32 rgb)
{
   int pass;
   int x_end = x;
   for (pass = 0; pass < 2; pass++)
   {
      int cx = x + (pass ? 0 : 1), cy = y + (pass ? 0 : 2);
      u32 col = pass ? rgb : 0x0A0A1E;
      const char *p = s;
      while (*p)
      {
         const ui_glyph *g = ui_glyph_of(f, ui_utf8_next(&p));
         const unsigned char *b = f->bits + g->off;
         int i, j, n = 0;
         for (j = 0; j < g->h; j++)
            for (i = 0; i < g->w; i++, n++)
            {
               int v = (b[n >> 1] >> ((n & 1) * 4)) & 15, px = cx + g->x + i, py = cy + g->y + j, k;
               u8 *d;
               if (!v || px < 0 || py < 0 || px >= w || py >= h)
                  continue;
               d = img + (py * w + px) * 4;
               v = v * 17 * (pass ? 255 : 150) / 255;
               {
                  /* over what's there, alpha too (the banner can be transparent) */
                  int da = d[3] * (255 - v) / 255, oa = v + da;
                  for (k = 0; k < 3; k++)
                  {
                     int c = (col >> (16 - 8 * k)) & 255;
                     d[k] = (u8)((c * v + d[k] * da) / oa);
                  }
                  d[3] = (u8)oa;
               }
            }
         cx += g->adv;
      }
      x_end = cx;
   }
   return x_end;
}

/* the game's name word-wrapped into at most rows lines of font f, each at
 * most maxw wide (" - " starts a new line, "..." where it's cut). Returns the
 * number of lines. */
static int wrap_name(const char *title, int sys, const ui_font *f, int maxw, char lines[][64], int rows)
{
   char clean[128];
   const char *s;
   int nl = 0;

   clean_name(title, clean, sizeof(clean));
   memset(lines, 0, rows * 64);
   s = clean;
   while (*s && nl < rows)
   {
      char line[64] = "";
      while (*s == ' ') s++;
      while (*s)
      {
         char test[64];
         const char *e = s;
         int w;
         while (*e && *e != ' ') e++;
         if (e - s == 1 && *s == '-')        /* " - " between parts: a new line */
         {
            s = e;
            if (line[0]) break;
            continue;
         }
         snprintf(test, sizeof(test), "%s%s%.*s", line, line[0] ? " " : "", (int)(e - s), s);
         w = ui_text_w(f, test);
         if (w > maxw && line[0])
            break;
         snprintf(line, sizeof(line), "%s", test);
         s = e;
         while (*s == ' ') s++;
         if (w > maxw)
            break;
      }
      if (!line[0])
         break;
      /* a word too long for the line: cut with "..." */
      while (ui_text_w(f, line) > maxw && strlen(line) > 4)
      {
         size_t n = strlen(line);
         line[n - 4] = '.'; line[n - 3] = '.'; line[n - 2] = '.'; line[n - 1] = 0;
      }
      if (nl == rows - 1 && *s)                     /* more left over: "..." */
      {
         size_t n = strlen(line);
         while (n > 4 && ui_text_w(f, line) + ui_text_w(f, "...") > maxw)
            line[--n] = 0;
         strcat(line, "...");
      }
      snprintf(lines[nl++], sizeof(lines[0]), "%s", line);
   }
   if (!nl)
      snprintf(lines[nl++], sizeof(lines[0]), "%s game", systems[sys].console);

   return nl;
}

/* the game's name on the generated banner, right of the icon: up to three
 * lines of Inter, then the emulator's name */
static void banner_name(const char *title, int sys, u8 *banner, int maxw)
{
   char lines[3][64];
   int nl = wrap_name(title, sys, &ui_font_bold, maxw, lines, 3), i, y;

   y = (128 - (nl * 20 + 22)) / 2;
   for (i = 0; i < nl; i++)
      rgba_text(banner, 256, 128, &ui_font_bold, 120, y + i * 20, lines[i], 0xFFFFFF);
   rgba_text(banner, 256, 128, &ui_font_small, 121, y + nl * 20 + 6, systems[sys].name, systems[sys].tag_rgb);
}

/* text centred at y with a dark outline, so it reads on any HOME Menu theme */
static void outlined_text(u8 *img, int w, int h, const ui_font *f, int y, const char *s, u32 rgb)
{
   static const signed char d[8][2] = { {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1} };
   int i, x = (w - ui_text_w(f, s)) / 2;
   for (i = 0; i < 8; i++)
      rgba_text(img, w, h, f, x + d[i][0] * 2, y + d[i][1] * 2, s, 0x0A0A1E);
   rgba_text(img, w, h, f, x, y, s, rgb);
}

/* the 3D banner's name strip (256x64, transparent): the name big on one
 * line with the system and emulator under it, or smaller on two lines */
static void name_strip(const char *title, int sys, u8 *strip)
{
   char lines[2][64], tag[64];
   int i;

   memset(strip, 0, 256 * 64 * 4);
   if (wrap_name(title, sys, &ui_font_big, 246, lines, 1) == 1 && !strstr(lines[0], "..."))
   {
      outlined_text(strip, 256, 64, &ui_font_big, 0, lines[0], 0xFFFFFF);
      snprintf(tag, sizeof(tag), "%s  \xC2\xB7  %s", systems[sys].console, systems[sys].name);
      outlined_text(strip, 256, 64, &ui_font_body, 44, tag, systems[sys].tag_rgb);
      return;
   }
   if (wrap_name(title, sys, &ui_font_title, 246, lines, 2) == 1)
   {
      outlined_text(strip, 256, 64, &ui_font_title, 8, lines[0], 0xFFFFFF);
      snprintf(tag, sizeof(tag), "%s  \xC2\xB7  %s", systems[sys].console, systems[sys].name);
      outlined_text(strip, 256, 64, &ui_font_body, 40, tag, systems[sys].tag_rgb);
      return;
   }
   for (i = 0; i < 2; i++)
      outlined_text(strip, 256, 64, &ui_font_title, 4 + i * 28, lines[i], 0xFFFFFF);
}

/* ------------------------------------------------------------------ */
/* the icon and banner a game gets                                     */
/* ------------------------------------------------------------------ */

typedef struct
{
   u8 icon48[48 * 48 * 4];
   u8 banner[256 * 128 * 4];     /* what the top screen previews (flat) */
   u8 name[256 * 64 * 4];        /* 3D: the name strip under the tile */
   u8 label[128 * 128 * 4];      /* 3D: the icon, on the tile */
   char title[64];
   int sys;
   bool has_icon_png, has_banner_png;
   bool icon_too_big, banner_too_big;   /* a picture too big to use, */
   int icon_big[2], banner_big[2];      /* and its size (0 x 0: not known) */
} game_art;

static void make_art(const char *path, const entry *e, game_art *a)
{
   const char *name = strrchr(path, '/') + 1, *dot = strrchr(name, '.');
   int n = (int)(dot - name), sys = e->sys, iw, ih, bw, bh;
   u8 *icon_png, *banner_png = NULL;
   bool big;

   snprintf(a->title, sizeof(a->title), "%.*s", n > 63 ? 63 : n, name);
   a->sys = sys;
   /* the picture at 128x128 first (the 3D tile, the generated banner), then
    * the icon from that: big pictures are scaled down only once */
   icon_png = load_picture(path, "", e->pic[0], 128, 128, &iw, &ih, &big);
   a->has_icon_png = icon_png != NULL;
   a->icon_too_big = big;
   a->icon_big[0] = iw;
   a->icon_big[1] = ih;
   if (icon_png)
   {
      fit_picture(icon_png, iw, ih, a->label, 128, 128);
      tm_mark("icon: scale to 128x128");
      image_fit(a->label, 128, 128, a->icon48, 48, 48);
      stbi_image_free(icon_png);
   }
   else
   {
      default_icon_style(a->title, systems[sys].style, a->icon48);
      image_fit(a->icon48, 48, 48, a->label, 128, 128);
   }
   tm_mark("icon: scale to 48x48");
   /* the 3D banner shows neither a banner picture nor the flat banner */
   a->has_banner_png = e->pic[1] >= 0;
   a->banner_too_big = false;
   if (banner_style != STYLE_3D)
   {
      banner_png = load_picture(path, ".banner", e->pic[1], 256, 128, &bw, &bh, &big);
      a->has_banner_png = banner_png != NULL;
      a->banner_too_big = big;
      a->banner_big[0] = bw;
      a->banner_big[1] = bh;
      if (banner_png)
      {
         fit_picture(banner_png, bw, bh, a->banner, 256, 128);
         stbi_image_free(banner_png);
      }
      else                             /* the icon, bigger, and the name */
      {
         default_banner(a->title, a->label, 128, 128, a->banner,
                        banner_style == STYLE_CLEAR ? BANNER_CLEAR : 0);
         banner_name(a->title, sys, a->banner, 256 - 120 - 8);
      }
      tm_mark("flat banner");
   }
   else
   {
      name_strip(a->title, sys, a->name);
      tm_mark("3D: name strip");
      {
         /* see-through parts take the tile's grey (make_banner3d.py's Body);
          * outside a square with rounded corners (radius 0.2 of the width,
          * as make_banner3d.py's CORNER) it's transparent: the corners cut round */
         int i, k;
         static const u8 body[3] = { 0x4A, 0x4F, 0x5A };
         for (i = 0; i < 128 * 128; i++)
         {
            u8 *q = a->label + i * 4;
            int x = i % 128, y = i / 128, r = 26, dx = 0, dy = 0;
            for (k = 0; k < 3; k++)
               q[k] = (u8)((q[k] * q[3] + body[k] * (255 - q[3])) / 255);
            if (x < r) dx = r - x; else if (x > 127 - r) dx = x - (127 - r);
            if (y < r) dy = r - y; else if (y > 127 - r) dy = y - (127 - r);
            q[3] = dx * dx + dy * dy > r * r ? 0 : 255;
         }
      }
      tm_mark("3D: tile");
   }
}

/* Make and install the icon for the game at path. Returns 0, or -1 with a
 * message in err. */
static int make_icon(const char *path, const entry *e, char *err, int errlen)
{
   static game_art art;
   int sys = e->sys;
   static u8 smdh[FWDCIA_SMDH_SIZE];
   char longt[160];
   const u8 *tsmdh;
   u8 *cia;
   bool three_d = banner_style == STYLE_3D;
   const u8 *tpl = three_d ? fwd_gpsp3d_3d_bin : fwd_gpsp3d_bin;
   u32 size = three_d ? fwd_gpsp3d_3d_bin_size : fwd_gpsp3d_bin_size, written;
   fwdcia_params p;
   Handle h;
   Result r;

   make_art(path, e, &art);
   snprintf(longt, sizeof(longt), "%.110s (%s)", art.title, systems[sys].name);

   cia = malloc(size);
   if (!cia)
   {
      snprintf(err, errlen, "Not enough memory.");
      return -1;
   }
   memcpy(cia, tpl, size);
   tsmdh = template_smdh(cia, size);
   if (!tsmdh)
   {
      snprintf(err, errlen, "The built-in template has no icon.");
      free(cia);
      return -1;
   }
   memcpy(smdh, tsmdh, sizeof(smdh));
   smdh_set(smdh, art.title, longt, systems[sys].name, art.icon48);

   memset(&p, 0, sizeof(p));
   p.title_id = fwdcia_title_id_for(path);
   p.target = systems[sys].tid;        /* the emulator to start */
   p.launch = systems[sys].launch;
   p.path = path;
   p.smdh = smdh;
   p.banner = three_d ? art.name : art.banner;
   p.label = three_d ? art.label : NULL;
   if (fwdcia_make(cia, size, &p, sha256_3ds, err, errlen))
   {
      free(cia);
      return -1;
   }

   r = AM_StartCiaInstall(MEDIATYPE_SD, &h);
   if (R_SUCCEEDED(r))
   {
      r = FSFILE_Write(h, &written, 0, cia, size, 0);
      if (R_SUCCEEDED(r) && written == size)
         r = AM_FinishCiaInstall(h);
      else
      {
         AM_CancelCIAInstall(h);
         if (R_SUCCEEDED(r))
            r = -1;
      }
   }
   free(cia);
   if (R_FAILED(r))
   {
      snprintf(err, errlen, "Install failed (%08lX).\nIt needs custom firmware with\nsignature patches (Luma3DS).",
               (unsigned long)r);
      return -1;
   }
   return 0;
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */
/* ------------------------------------------------------------------ */

static inline u16 rgb565(int r, int g, int b)
{
   return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* an RGBA picture onto the canvas, over what's there, scale 1 or 2 */
static void blit_rgba(const u8 *img, int w, int h, int x0, int y0, int scale)
{
   int x, y;
   for (y = 0; y < h * scale; y++)
      for (x = 0; x < w * scale; x++)
      {
         const u8 *s = img + ((y / scale) * w + x / scale) * 4;
         int cx = x0 + x, cy = y0 + y;
         u16 *d;
         if ((unsigned)cx >= UI_W || (unsigned)cy >= UI_H || !s[3])
            continue;
         d = &ui_canvas[cy * UI_W + cx];
         if (s[3] == 255)
            *d = rgb565(s[0], s[1], s[2]);
         else
         {
            int r = ((*d >> 11) << 3), g = (((*d >> 5) & 63) << 2), b = ((*d & 31) << 3);
            *d = rgb565((s[0] * s[3] + r * (255 - s[3])) / 255,
                        (s[1] * s[3] + g * (255 - s[3])) / 255,
                        (s[2] * s[3] + b * (255 - s[3])) / 255);
         }
      }
}

static void blit_565(const u16 *img, int w, int h, int x0, int y0, int scale)
{
   int x, y;
   for (y = 0; y < h * scale; y++)
      for (x = 0; x < w * scale; x++)
      {
         int cx = x0 + x, cy = y0 + y;
         if ((unsigned)cx < UI_W && (unsigned)cy < UI_H)
            ui_canvas[cy * UI_W + cx] = img[(y / scale) * w + x / scale];
      }
}

/* the canvas to the top screen (400 wide: the canvas in the middle, its
 * edge colours carried out to the sides) */
/* what the top screen shows, so the spinner can be drawn over it */
static u16 top_shown[UI_W * UI_H];

/* the canvas to the top screen (centred on its 400 pixels) */
static void present_top_canvas(void)
{
   u16 *fb = (u16*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
   int x, y;
   for (x = 0; x < 400; x++)
   {
      int cx = x - 40;
      u16 *col = fb + x * 240 + 239;
      if (cx < 0) cx = 0;
      if (cx >= UI_W) cx = UI_W - 1;
      for (y = 0; y < 240; y++)
         *col-- = ui_canvas[y * UI_W + cx];
   }
   GSPGPU_FlushDataCache(fb, 400 * 240 * 2);
   gfxScreenSwapBuffers(GFX_TOP, false);
}

static void present_top(void)
{
   memcpy(top_shown, ui_canvas, sizeof(top_shown));
   present_top_canvas();
}

/* a small SNES cartridge (wide, ridged sides) */
static void icon_snes(int x, int y, u16 c)
{
   ui_rrect(x, y + 1, 17, 14, 3, c);
   ui_fill(x + 4, y + 3, 9, 6, C_BG);
   ui_fill(x + 1, y + 11, 2, 1, C_BG);
   ui_fill(x + 14, y + 11, 2, 1, C_BG);
   ui_fill(x + 1, y + 13, 2, 1, C_BG);
   ui_fill(x + 14, y + 13, 2, 1, C_BG);
}

static void icon_folder(int x, int y, u16 c)
{
   ui_rrect(x, y + 2, 7, 4, 1, c);
   ui_rrect(x, y + 4, 16, 11, 2, c);
}

static void icon_cart(int x, int y, u16 c)
{
   ui_rrect(x + 1, y, 14, 15, 2, c);
   ui_fill(x + 4, y + 3, 8, 5, C_BG);
   ui_fill(x + 3, y + 11, 10, 1, C_BG);
}

/* text with \n, one line every dy; returns the y after it */
static int text_lines(const ui_font *f, int x, int y, int dy, const char *s, u16 c, int max_w)
{
   char line[128];
   while (s && *s)
   {
      const char *nl = strchr(s, '\n');
      int n = nl ? (int)(nl - s) : (int)strlen(s);
      if (n > 127) n = 127;
      memcpy(line, s, n);
      line[n] = 0;
      ui_text(f, x, y, line, c, max_w);
      y += dy;
      s += n + (nl ? 1 : 0);
      if (!nl) break;
   }
   return y;
}

static void draw_header(void)
{
   ui_vgradient(0, 0, UI_W, TAB_Y, C_BG2, C_BG);
   ui_text(&ui_font_title, 10, 3, "Icon Maker", C_TEXT, 0);
   ui_rrect(122, 8, 18, 15, 7, C_ORANGE);
   ui_fill(130, 11, 2, 9, C_TEXT);
   ui_fill(126, 14, 10, 2, C_TEXT);    /* a "+" badge */
#ifdef ICONMAKER_TIMING
   ui_text_right(&ui_font_small, UI_W - 10, 10, "TIMING BUILD   v" ICONMAKER_VERSION, C_WARN);
#else
   ui_text_right(&ui_font_small, UI_W - 10, 10, "GBA + SNES   v" ICONMAKER_VERSION, C_DIM);
#endif
}

static void draw_tabs(int tab)
{
   static const char *names[2] = { "Games", "My icons" };
   int i, tw = UI_W / 2;
   for (i = 0; i < 2; i++)
   {
      int x = i * tw, on = i == tab;
      char label[32];
      const ui_font *f = on ? &ui_font_bold : &ui_font_body;
      if (i == TAB_ICONS)
         snprintf(label, sizeof(label), "%s  %d", names[i], nicons);
      else
         snprintf(label, sizeof(label), "%s", names[i]);
      if (on)
      {
         ui_rrect(x + 6, TAB_Y, tw - 12, TAB_H, 10, C_ROW_SEL);
         ui_fill(x + tw / 2 - 14, TAB_Y + TAB_H - 3, 28, 3, C_ACCENT);
      }
      ui_text_vc(f, x + (tw - ui_text_w(f, label)) / 2, TAB_Y, TAB_H - 2, label, on ? C_TEXT : C_DIM, 0);
   }
}

static void draw_footer(const char *keys)
{
   ui_fill(0, FOOT_Y, UI_W, UI_H - FOOT_Y, C_BG2);
   ui_fill(0, FOOT_Y, UI_W, 1, C_PANEL);
   ui_text_vc(&ui_font_small, 10, FOOT_Y, UI_H - FOOT_Y, keys, C_DIM, UI_W - 20);
}

static void draw_scrollbar(int n, int top)
{
   if (n > ROWS)
   {
      int track = ROWS * ROW_H - 4;
      int bar = track * ROWS / n;
      if (bar < 12) bar = 12;
      ui_rrect(UI_W - 4, LIST_Y + (track - bar) * top / (n - ROWS), 3, bar, 1, C_FAINT);
   }
}

static void draw_bottom(int tab, int sel, int top)
{
   int i;
   ui_clear(C_BG);
   draw_header();
   draw_tabs(tab);
   if (tab == TAB_GAMES)
   {
      char info[64];
      static const char *const style_short[STYLE_COUNT] = { "blue", "no background", "3D" };
      int iw;
      snprintf(info, sizeof(info), "%d game%s   SELECT: %s   X: %s", ngames, ngames == 1 ? "" : "s",
               style_short[banner_style], fit_key[fit_mode]);
      iw = ui_text_w(&ui_font_small, info);
      ui_text(&ui_font_small, 10, LIST_Y - 18, cwd + 5, C_FAINT, UI_W - 30 - iw);
      ui_text_right(&ui_font_small, UI_W - 10, LIST_Y - 18, info, C_DIM);
      if (!nentries)
         ui_text_center(&ui_font_body, UI_W / 2, 130, "No folders or GBA / SNES games here", C_DIM);
      for (i = 0; i < ROWS && top + i < nentries; i++)
      {
         const entry *e = &entries[top + i];
         int y = LIST_Y + i * ROW_H, h = ROW_H - 2, s = top + i == sel, right = ROW_W - 50;
         if (s)
         {
            ui_rrect(ROW_X, y, ROW_W, h, 7, C_ROW_SEL);
            ui_fill(ROW_X, y + 5, 3, h - 10, C_ACCENT);
         }
         if (e->dir)
            icon_folder(ROW_X + 10, y + 3, s ? C_WARN : UI_RGB(0xB0, 0x8A, 0x45));
         else if (e->sys == SYS_SNES)
            icon_snes(ROW_X + 9, y + 3, s ? C_ACCENT2 : C_FAINT);
         else
            icon_cart(ROW_X + 10, y + 3, s ? C_ACCENT : C_FAINT);
         if (e->has_icon)
         {
            /* a green "on HOME" pill */
            int pw = ui_text_w(&ui_font_small, "on HOME") + 14;
            ui_rrect(ROW_X + ROW_W - pw - 8, y + 4, pw, h - 8, (h - 8) / 2, UI_RGB(0x1E, 0x3B, 0x2E));
            ui_text_vc(&ui_font_small, ROW_X + ROW_W - pw - 1, y + 4, h - 8, "on HOME", C_GOOD, 0);
            right = ROW_W - pw - 50;
         }
         ui_text_vc(&ui_font_body, ROW_X + 34, y, h, strcmp(e->name, "..") ? e->name : ".. (up a folder)",
                    s ? C_TEXT : C_TEXT2, right);
      }
      draw_scrollbar(nentries, top);
      draw_footer(entries[sel].dir || !nentries ? "A: open    Y: icons for every game here    R: My icons"
                                                : "A: make icon    Y: whole folder    B: up    R: My icons");
   }
   else
   {
      char info[64];
      snprintf(info, sizeof(info), "%d icon%s on the HOME Menu", nicons, nicons == 1 ? "" : "s");
      ui_text(&ui_font_small, 10, LIST_Y - 18, info, C_FAINT, 0);
      if (!nicons)
      {
         ui_text_center(&ui_font_body, UI_W / 2, 120, "No icons yet", C_DIM);
         ui_text_center(&ui_font_small, UI_W / 2, 142, "Make one in Games: pick a game, press A", C_FAINT);
      }
      for (i = 0; i < ROWS && top + i < nicons; i++)
      {
         const icon_entry *ic = &icons[top + i];
         int y = LIST_Y + i * ROW_H, h = ROW_H - 2, s = top + i == sel;
         if (s)
         {
            ui_rrect(ROW_X, y, ROW_W, h, 7, C_ROW_SEL);
            ui_fill(ROW_X, y + 5, 3, h - 10, C_ACCENT);
         }
         {
            /* the small icon, 24x24 shown at 19x19 by skipping rows/columns */
            int px, py;
            for (py = 0; py < 19; py++)
               for (px = 0; px < 19; px++)
                  ui_canvas[(y + 1 + py) * UI_W + ROW_X + 9 + px] = ic->icon24[(py * 24 / 19) * 24 + px * 24 / 19];
         }
         ui_text_vc(&ui_font_body, ROW_X + 36, y, h, ic->name, s ? C_TEXT : C_TEXT2, ROW_W - 90);
         ui_text_right(&ui_font_small, ROW_X + ROW_W - 10, y + (h - ui_font_small.ascent) / 2 - 1,
                       systems[ic->sys].console, ic->sys == SYS_SNES ? C_ACCENT2 : C_ACCENT);
      }
      draw_scrollbar(nicons, top);
      draw_footer(nicons ? "X: delete icon    L or B: Games" : "L or B: Games");
   }
}

/* ---- top screen: what the HOME Menu will show ---- */

static void top_bg(void)
{
   ui_vgradient(0, 0, UI_W, UI_H, C_BG2, C_BG);
}

static void draw_top_game(const game_art *a, bool installed)
{
   top_bg();
   ui_text(&ui_font_small, 32, 6, "HOME Menu preview", C_DIM, 0);
   ui_rrect_outline(30, 22, 260, 132, 6, 1, C_FAINT);
   if (banner_style == STYLE_3D)
   {
      /* the 3D banner: the icon on its tile, the name strip under it (half size) */
      static u8 half[128 * 32 * 4];
      int x, y, k;
      for (y = 0; y < 32; y++)
         for (x = 0; x < 128; x++)
            for (k = 0; k < 4; k++)
            {
               const u8 *p = a->name + ((y * 2) * 256 + x * 2) * 4 + k;
               half[(y * 128 + x) * 4 + k] = (u8)((p[0] + p[4] + p[256 * 4] + p[256 * 4 + 4]) / 4);
            }
      blit_rgba(a->label, 128, 128, 96, 24, 1);
      blit_rgba(half, 128, 32, 96, 121, 1);
   }
   else
      blit_rgba(a->banner, 256, 128, 32, 24, 1);

   ui_rrect(28, 164, 56, 56, 10, C_PANEL);
   blit_rgba(a->icon48, 48, 48, 32, 168, 1);
   ui_text(&ui_font_bold, 96, 164, a->title, C_TEXT, UI_W - 110);
   {
      /* a picture too big to use says so, and its size */
      char big[2][48];
      snprintf(big[0], sizeof(big[0]), a->icon_big[0] ? "Icon: picture too big (%dx%d)" : "Icon: picture too big",
               a->icon_big[0], a->icon_big[1]);
      snprintf(big[1], sizeof(big[1]), a->banner_big[0] ? "Banner: picture too big (%dx%d)" : "Banner: picture too big",
               a->banner_big[0], a->banner_big[1]);
      ui_text(&ui_font_small, 96, 186, a->icon_too_big ? big[0]
              : !a->has_icon_png ? "Icon: made from the name"
              : fit_mode == FIT_CROP ? "Icon: its picture, cropped (X)" : "Icon: its picture, padded (X)",
              a->icon_too_big ? C_WARN : C_DIM, 0);
      ui_text(&ui_font_small, 96, 201, banner_style != STYLE_3D && a->banner_too_big ? big[1]
              : a->has_banner_png && banner_style != STYLE_3D ? (fit_mode == FIT_CROP ? "Banner: its .banner picture, cropped (X)"
                                                                                      : "Banner: its .banner picture, padded (X)")
              : banner_style == STYLE_3D ? "Banner: 3D, the icon turns (SELECT)"
              : banner_style == STYLE_CLEAR ? "Banner: no background (SELECT)" : "Banner: blue background (SELECT)",
              banner_style != STYLE_3D && a->banner_too_big ? C_WARN : C_DIM, 0);
   }
   {
      char opens[80];
      if (sys_installed[a->sys])
         snprintf(opens, sizeof(opens), "Opens in %s", systems[a->sys].name);
      else
         snprintf(opens, sizeof(opens), "%s isn't installed as a CIA", systems[a->sys].name);
      ui_text(&ui_font_small, 96, 216, opens, sys_installed[a->sys] ? C_DIM : C_WARN, 0);
   }
   if (installed)
   {
      ui_rrect(220, 4, 70, 16, 8, UI_RGB(0x1E, 0x3B, 0x2E));
      ui_text_center(&ui_font_small, 255, 5, "on HOME", C_GOOD);
   }
}

static void draw_top_folder(const char *name, bool up)
{
   top_bg();
   ui_rrect(110, 40, 100, 70, 12, C_PANEL);
   ui_rrect(130, 58, 26, 12, 3, UI_RGB(0xB0, 0x8A, 0x45));
   ui_rrect(130, 64, 60, 36, 5, C_WARN);
   ui_text_center(&ui_font_bold, UI_W / 2, 124, up ? "Up a folder" : name, C_TEXT);
   if (!up)
      ui_text_center(&ui_font_small, UI_W / 2, 148, "A: open the folder", C_DIM);
}

static void draw_top_intro(void)
{
   top_bg();
   ui_text_center(&ui_font_title, UI_W / 2, 40, "Icon Maker", C_TEXT);
   ui_text_center(&ui_font_small, UI_W / 2, 68, "Puts GBA and SNES games on the HOME Menu", C_DIM);
   text_lines(&ui_font_small, 40, 100, 16,
              "Icon: Golf.png or .jpg next to Golf.gba,\n"
              "else a cartridge with the game's name.\n"
              "Banner: Golf.banner.png or .jpg (256x128),\n"
              "else the icon, bigger, with the name.\n"
              "Other shapes: X pads or crops them.\n\n"
              "GBA games open in gpSP 3D, SNES games\n"
              "in Snes9x 3D: install them as CIAs.", C_TEXT2, UI_W - 60);
}

static void draw_top_icon(const icon_entry *ic)
{
   static u8 smdh[FWDCIA_SMDH_SIZE];
   static u16 big[48 * 48];
   char tid[40];
   top_bg();
   ui_text(&ui_font_small, 32, 6, "On the HOME Menu", C_DIM, 0);
   ui_rrect(28, 30, 104, 104, 14, C_PANEL);
   if (read_title_smdh(ic->tid, smdh))
   {
      smdh_icon_decode(smdh + 0x24C0, 48, big);
      blit_565(big, 48, 48, 32, 34, 2);
   }
   ui_text(&ui_font_bold, 146, 40, ic->name, C_TEXT, UI_W - 160);
   snprintf(tid, sizeof(tid), "Title %016llX", (unsigned long long)ic->tid);
   ui_text(&ui_font_small, 146, 66, tid, C_DIM, 0);
   {
      char opens[64];
      snprintf(opens, sizeof(opens), "%s game, opens in %s", systems[ic->sys].console, systems[ic->sys].name);
      ui_text(&ui_font_small, 146, 82, opens, C_DIM, UI_W - 160);
   }
   text_lines(&ui_font_small, 146, 106, 15, "X: delete this icon.\nThe game file stays\non the SD card.", C_TEXT2, 0);
}

/* ---- dialogs on the bottom screen ---- */

static bool touched(u32 k, touchPosition *t)
{
   if (!(k & KEY_TOUCH))
      return false;
   hidTouchRead(t);
   return true;
}

/* a card over the bottom screen; ok / cancel labels (cancel may be NULL).
 * Returns true for A (or tapping ok), false for B (or tapping cancel). */
static bool dialog(const char *title, const char *body, const char *ok, const char *cancel, u16 ok_color)
{
   int lines = 1, h, y0, bw = cancel ? 128 : 160;
   const char *p;
   for (p = body; p && *p; p++)
      if (*p == '\n') lines++;
   h = 92 + lines * 16;
   y0 = (UI_H - h) / 2;

   ui_fill_alpha(0, 0, UI_W, UI_H, 0, 170);
   ui_rrect(14, y0, UI_W - 28, h, 14, C_PANEL);
   ui_text(&ui_font_bold, 30, y0 + 12, title, C_TEXT, UI_W - 60);
   text_lines(&ui_font_small, 30, y0 + 38, 16, body, C_TEXT2, UI_W - 60);
   if (cancel)
   {
      ui_rrect(30, y0 + h - 40, bw, 28, 14, C_ROW_SEL);
      ui_text_center(&ui_font_body, 30 + bw / 2, y0 + h - 36, cancel, C_TEXT2);
      ui_rrect(UI_W - 30 - bw, y0 + h - 40, bw, 28, 14, ok_color);
      ui_text_center(&ui_font_bold, UI_W - 30 - bw / 2, y0 + h - 37, ok, C_TEXT);
   }
   else
   {
      ui_rrect((UI_W - bw) / 2, y0 + h - 40, bw, 28, 14, ok_color);
      ui_text_center(&ui_font_bold, UI_W / 2, y0 + h - 37, ok, C_TEXT);
   }
   ui_present();

   while (aptMainLoop())
   {
      touchPosition t;
      u32 k;
      gspWaitForVBlank();
      hidScanInput();
      k = hidKeysDown();
      if (k & KEY_A) return true;
      if (k & KEY_B) return cancel ? false : true;
      if (touched(k, &t) && t.py >= y0 + h - 40 && t.py < y0 + h - 12)
      {
         if (!cancel && t.px >= (UI_W - bw) / 2 && t.px < (UI_W + bw) / 2) return true;
         if (cancel && t.px >= UI_W - 30 - bw && t.px < UI_W - 30) return true;
         if (cancel && t.px >= 30 && t.px < 30 + bw) return false;
      }
   }
   return false;
}

static void progress(const char *title, const char *line, int done, int n)
{
   int y0 = 70, w = UI_W - 76;
   ui_clear(C_BG);
   draw_header();
   ui_rrect(14, y0, UI_W - 28, 100, 14, C_PANEL);
   ui_text(&ui_font_bold, 30, y0 + 12, title, C_TEXT, UI_W - 60);
   ui_text(&ui_font_small, 30, y0 + 40, line, C_TEXT2, UI_W - 60);
   ui_rrect(38, y0 + 66, w, 8, 4, C_TRACK);
   if (n > 0 && done > 0)
      ui_rrect(38, y0 + 66, w * done / n < 8 ? 8 : w * done / n, 8, 4, C_ACCENT);
   draw_footer(n > 1 ? "Hold B to stop after this one" : "");
   ui_present();
}

/* ------------------------------------------------------------------ */
/* actions                                                             */
/* ------------------------------------------------------------------ */

static void make_one(const entry *e)
{
   const char *name = e->name;
   const emulator *em = &systems[e->sys];
   char path[800], err[200], body[400], missing[160] = "";
   bool again;
   snprintf(path, sizeof(path), "%s%s", cwd, name);
   again = icon_installed(fwdcia_title_id_for(path));
   if (!sys_installed[e->sys])
      snprintf(missing, sizeof(missing), "\n%s isn't installed as a CIA: the\nicon works once it is.", em->name);
   snprintf(body, sizeof(body), "%s\nIt opens in %s. The top screen\nshows how it will look.%s",
            again ? "It's on the HOME Menu already:\nthis makes it again." : "Its icon and banner go on the HOME Menu.",
            em->name, missing);
   if (!dialog(again ? "Make the icon again?" : "Make an icon?", body, "Make icon", "Cancel", C_ACCENT))
      return;
   progress("Making the icon...", name, 0, 1);
   if (make_icon(path, e, err, sizeof(err)))
      dialog("Could not make it", err, "OK", NULL, C_ROW_SEL);
   else
      dialog("Done", "It's on the HOME Menu now.", "OK", NULL, UI_RGB(0x2E, 0x8B, 0x57));
}

static void make_folder(void)
{
   char path[800], err[200], body[200], line[300], first_err[200] = "";
   int i, done = 0, failed = 0;
   if (!ngames)
   {
      dialog("No games here", "This folder has no GBA or SNES games.", "OK", NULL, C_ROW_SEL);
      return;
   }
   snprintf(body, sizeof(body), "All %d game%s in this folder.\nGames that are on the HOME Menu\nalready get made again.",
            ngames, ngames == 1 ? "" : "s");
   if (!dialog("Make icons for every game?", body, "Make all", "Cancel", C_ACCENT))
      return;
   for (i = 0; i < nentries && aptMainLoop(); i++)
   {
      if (entries[i].dir)
         continue;
      snprintf(line, sizeof(line), "%d of %d:  %s", done + failed + 1, ngames, entries[i].name);
      progress("Making icons...", line, done + failed, ngames);
      snprintf(path, sizeof(path), "%s%s", cwd, entries[i].name);
      if (make_icon(path, &entries[i], err, sizeof(err)))
      {
         if (!failed)
            snprintf(first_err, sizeof(first_err), "%s", err);
         failed++;
      }
      else
         done++;
      hidScanInput();
      if (hidKeysHeld() & KEY_B)
         break;
   }
   if (failed)
   {
      snprintf(body, sizeof(body), "%d made, %d failed. The first error:\n%.150s", done, failed, first_err);
      dialog("Some icons failed", body, "OK", NULL, C_ROW_SEL);
   }
   else
   {
      snprintf(body, sizeof(body), "%d icon%s on the HOME Menu now.", done, done == 1 ? "" : "s");
      dialog("Done", body, "OK", NULL, UI_RGB(0x2E, 0x8B, 0x57));
   }
}

static void delete_icon(const icon_entry *ic)
{
   char body[200];
   Result r;
   snprintf(body, sizeof(body), "%.60s\ncomes off the HOME Menu.\nThe game file stays on the SD card.", ic->name);
   if (!dialog("Delete this icon?", body, "Delete", "Cancel", UI_RGB(0xC8, 0x45, 0x45)))
      return;
   r = AM_DeleteTitle(MEDIATYPE_SD, ic->tid);
   if (R_SUCCEEDED(r))
      AM_DeleteTicket(ic->tid);
   if (R_FAILED(r))
   {
      snprintf(body, sizeof(body), "The system said %08lX.", (unsigned long)r);
      dialog("Could not delete it", body, "OK", NULL, C_ROW_SEL);
   }
}

static void save_dir(void)
{
   FILE *f;
   mkdir("sdmc:/3ds", 0777);
   mkdir("sdmc:/3ds/gpsp3d", 0777);
   if ((f = fopen(LAST_DIR, "w")))
   {
      fputs(cwd, f);
      fclose(f);
   }
}

static void save_settings(void)
{
   FILE *f;
   mkdir("sdmc:/3ds", 0777);
   mkdir("sdmc:/3ds/gpsp3d", 0777);
   if ((f = fopen(SETTINGS, "w")))
   {
      fprintf(f, "banner=%s\n", style_key[banner_style]);
      fprintf(f, "fit=%s\n", fit_key[fit_mode]);
      fclose(f);
   }
}

static void load_settings(void)
{
   FILE *f = fopen(SETTINGS, "r");
   char buf[128];
   if (!f)
      return;
   while (fgets(buf, sizeof(buf), f))
      if (!strncmp(buf, "banner=", 7))
      {
         int i;
         buf[strcspn(buf, "\r\n")] = 0;
         for (i = 0; i < STYLE_COUNT; i++)
            if (!strcmp(buf + 7, style_key[i]))
               banner_style = i;
      }
      else if (!strncmp(buf, "fit=", 4))
      {
         int i;
         buf[strcspn(buf, "\r\n")] = 0;
         for (i = 0; i < FIT_COUNT; i++)
            if (!strcmp(buf + 4, fit_key[i]))
               fit_mode = i;
      }
   fclose(f);
}

static void load_dir(void)
{
   FILE *f = fopen(LAST_DIR, "r");
   char buf[512];
   DIR *d;
   if (!f)
      return;
   if (fgets(buf, sizeof(buf), f))
   {
      buf[strcspn(buf, "\r\n")] = 0;
      if (!strncmp(buf, "sdmc:/", 6) && buf[strlen(buf) - 1] == '/' && (d = opendir(buf)))
      {
         closedir(d);
         strcpy(cwd, buf);
      }
   }
   fclose(f);
}

/* ------------------------------------------------------------------ */

/* The last previews made, so going back to a game shows it at once. Cleared
 * when the folder, the banner style or pad/crop changes. */
#define CACHE_N        16
#define PREVIEW_DELAY  18        /* frames (0.3 s) the selection must stay before a preview is made */
static struct { char path[800]; game_art *art; u32 used; } cache[CACHE_N];
static u32 cache_clock;

static void cache_clear(void)
{
   int i;
   for (i = 0; i < CACHE_N; i++)
      cache[i].path[0] = 0;
}

static game_art *cache_find(const char *path)
{
   int i;
   for (i = 0; i < CACHE_N; i++)
      if (cache[i].art && !strcmp(cache[i].path, path))
      {
         cache[i].used = ++cache_clock;
         return cache[i].art;
      }
   return NULL;
}

/* a slot for path's preview: an empty one, or the one unused longest */
static game_art *cache_slot(const char *path)
{
   int i, old = 0;
   for (i = 0; i < CACHE_N; i++)
      if (!cache[i].path[0] || cache[i].used < cache[old].used)
      {
         old = i;
         if (!cache[i].path[0])
            break;
      }
   if (!cache[old].art && !(cache[old].art = malloc(sizeof(game_art))))
      return NULL;
   snprintf(cache[old].path, sizeof(cache[0].path), "%s", path);
   cache[old].used = ++cache_clock;
   return cache[old].art;
}

/* frames to wait before the top screen shows what's selected: a game's
 * preview takes a while to make, so not while scrolling past it */
static int preview_wait(int tab, int sel)
{
   char path[800];
   if (tab != TAB_GAMES)
      return 8;
   if (!nentries || entries[sel].dir)
      return 1;
   snprintf(path, sizeof(path), "%s%s", cwd, entries[sel].name);
   return cache_find(path) ? 1 : PREVIEW_DELAY;
}

static void cache_drop(const game_art *a)
{
   int i;
   for (i = 0; i < CACHE_N; i++)
      if (cache[i].art == a)
         cache[i].path[0] = 0;
}

/* A button pressed while a preview is made stops it (a huge PNG can take
 * seconds); the main loop still gets the button. Called every 16 rows of a
 * PNG, which also turns the spinner once the preview takes a while. */
static u32 preview_keys;
static bool preview_stopped;

#define SPINNER_AFTER  250       /* ms a preview takes before the spinner shows */
#define SPINNER_EVERY  100       /* ms between its frames */
#define MS_TICKS(ms)   ((u64)(ms) * (SYSCLOCK_ARM11 / 1000))

static u16 mix565(u16 a, u16 b, int t)   /* t 0..256: a .. b */
{
   int r = ((a >> 11) * (256 - t) + (b >> 11) * t) >> 8;
   int g = (((a >> 5) & 63) * (256 - t) + ((b >> 5) & 63) * t) >> 8;
   int bl = ((a & 31) * (256 - t) + (b & 31) * t) >> 8;
   return (u16)(r << 11 | g << 5 | bl);
}

/* the top screen as it is, with a small "loading" pill at the bottom: eight
 * dots going round, and that any button skips */
static void draw_spinner(u64 now)
{
   static const signed char dx[8] = { 6, 4, 0, -4, -6, -4, 0, 4 }, dy[8] = { 0, 4, 6, 4, 0, -4, -6, -4 };
   static const char msg[] = "Loading the cover...   any button skips";
   int i, w = ui_text_w(&ui_font_small, msg) + 40, x = (UI_W - w) / 2, y = UI_H - 30;
   int head = (int)((now - preview_t0) / MS_TICKS(SPINNER_EVERY)) % 8;
   memcpy(ui_canvas, top_shown, sizeof(top_shown));
   ui_rrect(x, y, w, 22, 11, C_PANEL);
   for (i = 0; i < 8; i++)
      ui_circle(x + 14 + dx[i], y + 11 + dy[i], 2, mix565(C_ACCENT, C_PANEL, ((head - i + 8) % 8) * 28));
   ui_text_vc(&ui_font_small, x + 28, y, 22, msg, C_TEXT2, 0);
   present_top_canvas();
}

static int preview_stop(void)
{
   u64 now = svcGetSystemTick();
   u32 k;
   if (now - preview_t0 > MS_TICKS(SPINNER_AFTER) && now - spinner_last > MS_TICKS(SPINNER_EVERY))
   {
      draw_spinner(now);
      spinner_last = svcGetSystemTick();
      spinner_ticks += spinner_last - now;
   }
   hidScanInput();
   if (!(k = hidKeysDown()))
      return 0;
   preview_keys |= k;
   preview_stopped = true;
   return 1;
}

/* The top screen for what's selected. Returns false if a button stopped
 * it (the top screen is left as it was). */
static bool draw_top_for(int tab, int sel, bool am)
{
   static game_art art;
   if (!am)
   {
      top_bg();
      ui_text_center(&ui_font_title, UI_W / 2, 70, "Can't install", C_BAD);
      text_lines(&ui_font_small, 40, 110, 16, "No access to the install service.\n"
                 "Start Icon Maker from the HOME Menu\n(install its CIA) or the Homebrew\n"
                 "Launcher on Luma3DS.", C_TEXT2, UI_W - 60);
   }
   else if (tab == TAB_ICONS)
   {
      if (nicons)
         draw_top_icon(&icons[sel]);
      else
         draw_top_intro();
   }
   else if (!nentries)
      draw_top_intro();
   else if (entries[sel].dir)
      draw_top_folder(entries[sel].name, !strcmp(entries[sel].name, ".."));
   else
   {
      char path[800];
      game_art *a;
      snprintf(path, sizeof(path), "%s%s", cwd, entries[sel].name);
      tm_start();
      if ((a = cache_find(path)))
         tm_mark("from the cache");
      else
      {
         if (!(a = cache_slot(path)))
            a = &art;
         preview_stopped = false;
         preview_t0 = svcGetSystemTick();
         spinner_last = spinner_ticks = 0;
         png_shrink_stop = preview_stop;
         make_art(path, &entries[sel], a);
         png_shrink_stop = NULL;
         if (preview_stopped)
         {
            cache_drop(a);
#ifdef ICONMAKER_TIMING
            {
               char line[400];
               u32 total = 0;
               int i;
               for (i = 0; i < ntm; i++)
                  if (tm[i].n > 0)
                     total += tm[i].us;
               snprintf(line, sizeof(line), "preview %s: stopped by a button after %.1f ms\n",
                        entries[sel].name, total / 1000.0);
               tm_log(line);
            }
#endif
            return false;
         }
      }
      draw_top_game(a, entries[sel].has_icon);
      tm_mark("draw the top screen");
#ifdef ICONMAKER_TIMING
      {
         /* the steps on a dark panel over the right of the preview */
         char line[96], log[2000];
         int i, y = 4, n;
         u32 total = 0;
         for (i = 0; i < ntm; i++)
            if (tm[i].n > 0)                /* not the parts measured inside a step */
               total += tm[i].us;
         ui_fill_alpha(130, 0, UI_W - 130, UI_H, C_BG, 230);
         snprintf(line, sizeof(line), "total %.0f ms", total / 1000.0);
         ui_text(&ui_font_bold, 136, y, line, C_WARN, 0);
         y += 20;
         for (i = 0; i < ntm; i++, y += 12)
         {
            snprintf(line, sizeof(line), "%6.1f  %s%s", tm[i].us / 1000.0, tm[i].what,
                     tm[i].n > 1 ? " (x" : "");
            if (tm[i].n > 1)
               snprintf(line + strlen(line), sizeof(line) - strlen(line), "%d)", tm[i].n);
            ui_text(&ui_font_small, 136, y, line, C_TEXT2, UI_W - 140);
         }
         y += 4;
         for (i = 0; i < 2; i++)
            if (tm_pic[i][0])
            {
               snprintf(line, sizeof(line), "%s: %s", i ? "banner" : "icon", tm_pic[i]);
               ui_text(&ui_font_small, 136, y, line, C_DIM, UI_W - 140);
               y += 12;
            }
         snprintf(line, sizeof(line), "folder read %.0f ms (%lu)", tm_dir_us / 1000.0,
                  (unsigned long)tm_dir_n);
         ui_text(&ui_font_small, 136, y, line, C_DIM, 0);
         snprintf(line, sizeof(line), "bottom redraw %.1f ms", tm_bottom_us / 1000.0);
         ui_text(&ui_font_small, 136, y + 12, line, C_DIM, 0);

         /* the same in the log, one step a line */
         n = snprintf(log, sizeof(log), "preview %s: %.1f ms\n", entries[sel].name, total / 1000.0);
         for (i = 0; i < 2; i++)
            if (tm_pic[i][0])
               n += snprintf(log + n, sizeof(log) - n, "   %s: %s\n", i ? "banner" : "icon", tm_pic[i]);
         for (i = 0; i < ntm && n < (int)sizeof(log) - 80; i++)
            n += snprintf(log + n, sizeof(log) - n, "   %8.1f ms  %s%s\n", tm[i].us / 1000.0, tm[i].what,
                          tm[i].n > 1 ? " (several times)" : "");
         tm_log(log);
      }
#endif
   }
   present_top();
   return true;
}

int main(void)
{
   int tab = TAB_GAMES, sel = 0, top = 0, gsel = 0, gtop = 0;
   int top_wait = 0;           /* frames until the top screen is redrawn */
   bool am, redraw = true;

   osSetSpeedupEnable(true);           /* New 3DS: the faster CPU and its L2 cache */
   gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
   am = R_SUCCEEDED(amInit());
   hidSetRepeatParameters(20, 4);

   if (am)
      read_icons();
   load_settings();
#ifdef ICONMAKER_TIMING
   tm_log_start();
   png_shrink_clock = svcGetSystemTick;
#endif
   load_dir();
   read_dir();
   top_wait = 1;

   while (aptMainLoop())
   {
      u32 k;
      touchPosition t;
      int count, old_sel = sel, old_tab = tab;
      bool act = false;

      hidScanInput();
      k = hidKeysDown() | (hidKeysDownRepeat() & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT)) | preview_keys;
      preview_keys = 0;
      count = tab == TAB_GAMES ? nentries : nicons;
      if (k & KEY_START)
         break;
      if (k & KEY_DDOWN && sel < count - 1) sel++;
      if (k & KEY_DUP && sel > 0) sel--;
      if (k & KEY_DRIGHT) sel = sel + ROWS < count ? sel + ROWS : count - 1;
      if (k & KEY_DLEFT) sel = sel > ROWS ? sel - ROWS : 0;

      /* touch: tabs, rows (tap the selected row again to use it) */
      if (touched(k, &t))
      {
         if (t.py >= TAB_Y && t.py < TAB_Y + TAB_H)
            k |= t.px < UI_W / 2 ? (tab == TAB_ICONS ? KEY_L : 0) : (tab == TAB_GAMES ? KEY_R : 0);
         else if (t.py >= LIST_Y && t.py < LIST_Y + ROWS * ROW_H)
         {
            int idx = top + (t.py - LIST_Y) / ROW_H;
            if (idx < count)
            {
               if (idx == sel)
                  act = true;
               sel = idx;
            }
         }
      }

      if (tab == TAB_GAMES)
      {
         if (k & KEY_R && am)
         {
            gsel = sel;
            gtop = top;
            tab = TAB_ICONS;
            sel = top = 0;
         }
         else if (k & KEY_B && strcmp(cwd, "sdmc:/"))
         {
            enter("..");
            sel = top = 0;
         }
         else if (k & KEY_SELECT)
         {
            /* the banner style, for icons made from now on */
            banner_style = (banner_style + 1) % STYLE_COUNT;
            cache_clear();
            save_settings();
            top_wait = 1;
            redraw = true;
         }
         else if (k & KEY_X)
         {
            /* pad or crop pictures of another shape, for icons made from now on */
            fit_mode = (fit_mode + 1) % FIT_COUNT;
            cache_clear();
            save_settings();
            top_wait = 1;
            redraw = true;
         }
         else if (k & KEY_Y && am)
         {
            make_folder();
            read_icons();
            read_dir();
            redraw = true;
         }
         else if ((k & KEY_A || act) && nentries)
         {
            if (entries[sel].dir)
            {
               enter(entries[sel].name);
               sel = top = 0;
            }
            else if (am)
            {
               make_one(&entries[sel]);
               read_icons();
               read_dir();
               top_wait = 1;
            }
            redraw = true;
         }
      }
      else
      {
         if (k & (KEY_L | KEY_B))
         {
            tab = TAB_GAMES;
            read_dir();
            sel = gsel < nentries ? gsel : 0;
            top = gtop;
         }
         else if ((k & KEY_X || act) && nicons)
         {
            delete_icon(&icons[sel]);
            read_icons();
            redraw = true;
            top_wait = 1;
         }
      }

      count = tab == TAB_GAMES ? nentries : nicons;
      if (sel >= count) sel = count - 1;
      if (sel < 0) sel = 0;
      if (sel < top) top = sel;
      if (sel >= top + ROWS) top = sel - ROWS + 1;
      if (top > count - ROWS) top = count - ROWS > 0 ? count - ROWS : 0;

      if (sel != old_sel || tab != old_tab || redraw || k)
      {
#ifdef ICONMAKER_TIMING
         u64 t_bottom = svcGetSystemTick();
#endif
         draw_bottom(tab, sel, top);
         ui_present();
#ifdef ICONMAKER_TIMING
         tm_bottom_us = tm_us(svcGetSystemTick() - t_bottom);
         if (tm_bottom_us > 16700)             /* longer than a frame */
         {
            char line[80];
            snprintf(line, sizeof(line), "bottom screen redraw: %.1f ms\n", tm_bottom_us / 1000.0);
            tm_log(line);
         }
#endif
         if (sel != old_sel || tab != old_tab)
            top_wait = preview_wait(tab, sel); /* wait for the selection to settle */
         else if (redraw && !top_wait)
            top_wait = preview_wait(tab, sel);
         redraw = false;
      }
      /* the top preview, once the selection stops moving */
      if (top_wait && !--top_wait)
      {
         /* stopped by a button: made again once the selection settles */
         if (!draw_top_for(tab, sel, am))
            top_wait = preview_wait(tab, sel);
         draw_bottom(tab, sel, top);       /* the canvas is shared: draw it back */
         ui_present();
      }
      gspWaitForVBlank();
   }

   save_dir();
   if (am)
      amExit();
   gfxExit();
   return 0;
}
