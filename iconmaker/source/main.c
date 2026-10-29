/*
 * Icon Maker: puts GBA and SNES games on the HOME Menu, right from the 3DS.
 *
 * Pick a game; the app builds a small forwarder CIA for it (from the
 * template built into it, see fwdcia.h) and installs it. Started from the
 * HOME Menu, the forwarder opens the game's emulator straight into it:
 * gpSP 3D for GBA games, Snes9x 3D for SNES games (it writes the game's
 * path to the file the emulator reads at startup, then starts it).
 *
 * Name: the game's file name. Icon: a PNG next to the game with the same
 * name (Golf.gba -> Golf.png, any size, best square), else a cartridge
 * with the game's name on it. Top screen banner: Golf.banner.png (best
 * 256x128), else the icon, bigger, with the name. The top screen shows
 * both before anything is made.
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
#include "fwdcia.h"
#include "deficon.h"
#include "ui.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
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

typedef struct { char name[256]; bool dir, has_icon; int sys; } entry;
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

static int cmp_entry(const void *a, const void *b)
{
   const entry *x = a, *y = b;
   if (x->dir != y->dir)
      return x->dir ? -1 : 1;
   return strcasecmp(x->name, y->name);
}

static void read_dir(void)
{
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
   while ((e = readdir(d)) && nentries < MAX_ENTRIES)
   {
      bool dir = e->d_type == DT_DIR;
      char path[800];
      int sys = -1;
      if (e->d_name[0] == '.')
         continue;
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
   qsort(entries + (strcmp(cwd, "sdmc:/") ? 1 : 0),
         nentries - (strcmp(cwd, "sdmc:/") ? 1 : 0), sizeof(entry), cmp_entry);
}

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

/* the game's picture with this ending ("png" or "banner.png"), or NULL */
static u8 *load_png(const char *game, const char *ext, int *w, int *h)
{
   char png[600];
   const char *dot = strrchr(game, '.');
   int n;
   snprintf(png, sizeof(png), "%.*s.%s", (int)(dot - game), game, ext);
   return stbi_load(png, w, h, &n, 4);
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

/* the game's name on the generated banner, right of the icon: up to three
 * lines of Inter, then the emulator's name */
static void banner_name(const char *title, int sys, u8 *banner, int maxw)
{
   const ui_font *f = &ui_font_bold;
   char clean[128], lines[3][64];
   const char *s;
   int nl = 0, i, y;

   clean_name(title, clean, sizeof(clean));
   memset(lines, 0, sizeof(lines));
   s = clean;
   while (*s && nl < 3)
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
      if (nl == 2 && *s)                     /* more left over: "..." */
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

   y = (128 - (nl * 20 + 22)) / 2;
   for (i = 0; i < nl; i++)
      rgba_text(banner, 256, 128, f, 120, y + i * 20, lines[i], 0xFFFFFF);
   rgba_text(banner, 256, 128, &ui_font_small, 121, y + nl * 20 + 6, systems[sys].name, systems[sys].tag_rgb);
}

/* ------------------------------------------------------------------ */
/* the icon and banner a game gets                                     */
/* ------------------------------------------------------------------ */

typedef struct
{
   u8 icon48[48 * 48 * 4];
   u8 banner[256 * 128 * 4];     /* what the top screen previews (flat) */
   u8 back[256 * 128 * 4];       /* 3D: the backdrop, without the icon */
   u8 label[128 * 128 * 4];      /* 3D: the icon, on the tile */
   char title[64];
   int sys;
   bool has_icon_png, has_banner_png;
} game_art;

static void make_art(const char *path, int sys, game_art *a)
{
   const char *name = strrchr(path, '/') + 1, *dot = strrchr(name, '.');
   int n = (int)(dot - name), iw = 48, ih = 48, bw, bh;
   u8 *icon_png, *banner_png;

   snprintf(a->title, sizeof(a->title), "%.*s", n > 63 ? 63 : n, name);
   a->sys = sys;
   icon_png = load_png(path, "png", &iw, &ih);
   a->has_icon_png = icon_png != NULL;
   if (icon_png)
      icon_scale48(icon_png, iw, ih, a->icon48);
   else
   {
      default_icon_style(a->title, systems[sys].style, a->icon48);
      iw = ih = 48;
   }
   banner_png = load_png(path, "banner.png", &bw, &bh);
   a->has_banner_png = banner_png != NULL;
   if (banner_png)
      banner_from_image(banner_png, bw, bh, a->banner);
   else                                /* the icon, bigger, and the name */
   {
      default_banner(a->title, icon_png ? icon_png : a->icon48, iw, ih, a->banner,
                     banner_style == STYLE_CLEAR ? BANNER_CLEAR : 0);
      banner_name(a->title, sys, a->banner, 256 - 120 - 8);
   }
   if (banner_style == STYLE_3D)
   {
      if (banner_png)
         banner_from_image(banner_png, bw, bh, a->back);
      else
      {
         default_banner(a->title, NULL, 0, 0, a->back, BANNER_NO_ICON);
         banner_name(a->title, sys, a->back, 232 - 120);   /* the 3D view shows x 21..235 */
      }
      image_fit(icon_png ? icon_png : a->icon48, iw, ih, a->label, 128, 128);
      {
         /* see-through parts take the tile's grey (make_banner3d.py's Body) */
         int i, k;
         static const u8 body[3] = { 0x4A, 0x4F, 0x5A };
         for (i = 0; i < 128 * 128; i++)
         {
            u8 *q = a->label + i * 4;
            for (k = 0; k < 3; k++)
               q[k] = (u8)((q[k] * q[3] + body[k] * (255 - q[3])) / 255);
            q[3] = 255;
         }
      }
   }
   if (icon_png)
      stbi_image_free(icon_png);
   if (banner_png)
      stbi_image_free(banner_png);
}

/* Make and install the icon for the game at path. Returns 0, or -1 with a
 * message in err. */
static int make_icon(const char *path, int sys, char *err, int errlen)
{
   static game_art art;
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

   make_art(path, sys, &art);
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
   p.banner = three_d ? art.back : art.banner;
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
static void present_top(void)
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
   ui_text_right(&ui_font_small, UI_W - 10, 10, "GBA + SNES   v" ICONMAKER_VERSION, C_DIM);
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
      snprintf(info, sizeof(info), "%d game%s   SELECT: banner %s", ngames, ngames == 1 ? "" : "s",
               style_short[banner_style]);
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
   blit_rgba(a->banner, 256, 128, 32, 24, 1);

   ui_rrect(28, 164, 56, 56, 10, C_PANEL);
   blit_rgba(a->icon48, 48, 48, 32, 168, 1);
   ui_text(&ui_font_bold, 96, 164, a->title, C_TEXT, UI_W - 110);
   ui_text(&ui_font_small, 96, 186, a->has_icon_png ? "Icon: its PNG" : "Icon: made from the name", C_DIM, 0);
   ui_text(&ui_font_small, 96, 201, a->has_banner_png ? "Banner: its .banner.png"
           : banner_style == STYLE_3D ? "Banner: 3D, the icon pops out (SELECT)"
           : banner_style == STYLE_CLEAR ? "Banner: no background (SELECT)" : "Banner: blue background (SELECT)",
           C_DIM, 0);
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
              "Icon: Golf.png next to Golf.gba (best square),\n"
              "else a cartridge with the game's name.\n"
              "Banner: Golf.banner.png (best 256x128),\n"
              "else the icon, bigger, with the name.\n\n"
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

static bool touched(touchPosition *t)
{
   if (!(hidKeysDown() & KEY_TOUCH))
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
      if (touched(&t) && t.py >= y0 + h - 40 && t.py < y0 + h - 12)
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
   if (make_icon(path, e->sys, err, sizeof(err)))
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
      if (make_icon(path, entries[i].sys, err, sizeof(err)))
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
      fprintf(f, "banner_style=%s\n", style_key[banner_style]);
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
      if (!strncmp(buf, "banner_style=", 13))
      {
         int i;
         buf[strcspn(buf, "\r\n")] = 0;
         for (i = 0; i < STYLE_COUNT; i++)
            if (!strcmp(buf + 13, style_key[i]))
               banner_style = i;
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

static void draw_top_for(int tab, int sel, bool am)
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
      snprintf(path, sizeof(path), "%s%s", cwd, entries[sel].name);
      make_art(path, entries[sel].sys, &art);
      draw_top_game(&art, entries[sel].has_icon);
   }
   present_top();
}

int main(void)
{
   int tab = TAB_GAMES, sel = 0, top = 0, gsel = 0, gtop = 0;
   int top_wait = 0;           /* frames until the top screen is redrawn */
   bool am, redraw = true;

   gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
   am = R_SUCCEEDED(amInit());
   hidSetRepeatParameters(20, 4);

   if (am)
      read_icons();
   load_settings();
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
      k = hidKeysDown() | (hidKeysDownRepeat() & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT));
      count = tab == TAB_GAMES ? nentries : nicons;
      if (k & KEY_START)
         break;
      if (k & KEY_DDOWN && sel < count - 1) sel++;
      if (k & KEY_DUP && sel > 0) sel--;
      if (k & KEY_DRIGHT) sel = sel + ROWS < count ? sel + ROWS : count - 1;
      if (k & KEY_DLEFT) sel = sel > ROWS ? sel - ROWS : 0;

      /* touch: tabs, rows (tap the selected row again to use it) */
      if (touched(&t))
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
         draw_bottom(tab, sel, top);
         ui_present();
         if (sel != old_sel || tab != old_tab || redraw)
            top_wait = 8;                      /* wait for the selection to settle */
         redraw = false;
      }
      /* the top preview, once the selection stops moving (loading PNGs) */
      if (top_wait && !--top_wait)
      {
         draw_top_for(tab, sel, am);
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
