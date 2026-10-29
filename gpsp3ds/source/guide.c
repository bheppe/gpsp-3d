/* Guide viewer: a plain-text guide for the game on the bottom screen.
 *
 * The text is read once, turned into the characters the font has (UTF-8
 * and Windows-1252 accents, quotes, dashes and box drawing become their
 * ASCII look-alikes) and wrapped to the screen width; after that drawing
 * is just the dozen lines that are visible. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include "guide.h"
#include "ui.h"

#define C_BG      UI_RGB(0x0E, 0x11, 0x17)
#define C_BG2     UI_RGB(0x16, 0x1A, 0x23)
#define C_PANEL   UI_RGB(0x1B, 0x20, 0x2A)
#define C_ACCENT  UI_RGB(0x4E, 0x8D, 0xFF)
#define C_TEXT    UI_RGB(0xEC, 0xEF, 0xF4)
#define C_DIM     UI_RGB(0x8B, 0x94, 0xA7)
#define C_FAINT   UI_RGB(0x4A, 0x52, 0x63)
#define C_TRACK   UI_RGB(0x37, 0x3F, 0x50)

#define GUIDE_MAX   (3 * 1024 * 1024)
#define HEAD_H      30
#define FOOT_Y      208
#define TEXT_X      8
#define TEXT_W      284           /* wrap width; the scroll bar is to the right */
#define LINE_H      14
#define BAR_X       298           /* touches right of this move the scroll bar */
#define AREA_Y      (HEAD_H + 2)
#define AREA_H      (FOOT_Y - 2 - AREA_Y)

typedef struct { uint32_t off; uint16_t len; } gline;

bool guide_shown;
static char  *text;
static gline *lines;
static int    nlines, scroll_px;
static char   g_file[600], g_pos[600];
static const char *g_err;
static int    t_mode, t_last_y;   /* 0 none, 1 drag, 2 scroll bar */
static float  t_vel, fling;       /* drag speed and the speed it keeps after letting go, px/frame */
static int    drawn_scroll = -1;  /* scroll position the canvas shows, -1 = redraw everything */

bool guide_available(void) { return text != NULL; }
const char *guide_file(void) { return g_file; }
const char *guide_error(void) { return g_err; }

static int adv(char c)
{
   unsigned char u = (unsigned char)c;
   if (u < 32 || u > 126) u = '?';
   return ui_font_small.g[u - 32].adv;
}

/* one code point -> the ASCII that looks most like it (written to o) */
static int ascii_of(uint32_t cp, char *o)
{
   static const char latin[] =
      "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYTs" "aaaaaaaceeeeiiii" "dnooooo/ouuuuyty";
   if (cp < 128) { o[0] = (char)cp; return 1; }
   if (cp >= 0xC0 && cp <= 0xFF) { o[0] = latin[cp - 0xC0]; return 1; }
   switch (cp)
   {
      case 0xA0: o[0] = ' '; return 1;
      case 0xB4: case 0x2018: case 0x2019: case 0x201A: case 0x2032: o[0] = '\''; return 1;
      case 0xAB: case 0xBB: case 0x201C: case 0x201D: case 0x201E: case 0x2033: o[0] = '"'; return 1;
      case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2212: o[0] = '-'; return 1;
      case 0x2022: case 0x2605: case 0x2606: case 0xB7: o[0] = '*'; return 1;
      case 0x2026: memcpy(o, "...", 3); return 3;
      case 0x2190: o[0] = '<'; return 1;
      case 0x2192: o[0] = '>'; return 1;
      case 0x2191: o[0] = '^'; return 1;
      case 0x2193: o[0] = 'v'; return 1;
      case 0xD7: o[0] = 'x'; return 1;
      case 0xB0: o[0] = 'o'; return 1;
   }
   if (cp >= 0x2500 && cp <= 0x257F)   /* box drawing */
   {
      if (cp == 0x2500 || cp == 0x2501 || cp == 0x2504 || cp == 0x2505) o[0] = '-';
      else if (cp == 0x2502 || cp == 0x2503 || cp == 0x2506 || cp == 0x2507 || cp == 0x2551) o[0] = '|';
      else if (cp == 0x2550) o[0] = '=';
      else o[0] = '+';
      return 1;
   }
   if (cp >= 0x2580 && cp <= 0x259F) { o[0] = '#'; return 1; }   /* blocks */
   o[0] = '?';
   return 1;
}

/* Windows-1252 bytes that aren't valid UTF-8 */
static char cp1252(unsigned char b)
{
   switch (b)
   {
      case 0x91: case 0x92: case 0xB4: return '\'';
      case 0x93: case 0x94: case 0xAB: case 0xBB: return '"';
      case 0x96: case 0x97: return '-';
      case 0x95: case 0xB7: return '*';
      case 0x85: return '.';
      case 0xA0: return ' ';
   }
   if (b >= 0xC0) { char o; ascii_of(b, &o); return o; }
   return '?';
}

/* in place: never writes past what it has read (the longest output, "...",
 * comes from three input bytes) */
static uint32_t clean(char *s, uint32_t n)
{
   uint32_t i = 0, o = 0;
   if (n >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
      i = 3;   /* byte order mark */
   while (i < n)
   {
      unsigned char b = (unsigned char)s[i];
      if (b < 0x80)
      {
         i++;
         if (b == '\r') { if (i < n && s[i] == '\n') continue; b = '\n'; }
         if (b == '\t') b = ' ';
         else if (b < 32 && b != '\n') continue;
         s[o++] = (char)b;
         continue;
      }
      {
         int need = b >= 0xF0 && b <= 0xF4 ? 3 : b >= 0xE0 ? (b <= 0xEF ? 2 : -1) : b >= 0xC2 ? 1 : -1;
         uint32_t cp = need == 3 ? b & 7 : need == 2 ? b & 15 : b & 31;
         int k, ok = need > 0 && i + need < n;
         for (k = 1; ok && k <= need; k++)
         {
            unsigned char c = (unsigned char)s[i + k];
            if ((c & 0xC0) != 0x80) ok = 0;
            else cp = (cp << 6) | (c & 63);
         }
         if (ok)
         {
            char tmp[3];
            int m = ascii_of(cp, tmp);
            i += need + 1;
            memcpy(s + o, tmp, m);
            o += m;
         }
         else
         {
            s[o++] = cp1252(b);
            i++;
         }
      }
   }
   s[o] = 0;
   return o;
}

static int add_line(uint32_t off, uint32_t len, int *cap)
{
   if (nlines == *cap)
   {
      int nc = *cap ? *cap * 2 : 4096;
      gline *nl = realloc(lines, nc * sizeof(gline));
      if (!nl) return -1;
      lines = nl; *cap = nc;
   }
   lines[nlines].off = off;
   lines[nlines].len = (uint16_t)len;
   nlines++;
   return 0;
}

static int wrap(uint32_t n)
{
   uint32_t p = 0;
   int cap = 0;
   nlines = 0;
   while (p <= n)
   {
      uint32_t end = p, start = p;
      while (end < n && text[end] != '\n') end++;
      if (end == start && add_line(start, 0, &cap)) return -1;
      while (start < end)
      {
         uint32_t q = start, brk = 0;
         int w = 0;
         while (q < end && q - start < 1000)
         {
            int a = adv(text[q]);
            if (w + a > TEXT_W) break;
            w += a;
            if (text[q] == ' ' && q > start) brk = q;
            q++;
         }
         if (q < end && brk && text[q] != ' ')
         {
            if (add_line(start, brk - start, &cap)) return -1;
            start = brk + 1;
         }
         else
         {
            if (add_line(start, q - start, &cap)) return -1;
            start = q;
            if (start < end && text[start] == ' ') start++;
         }
      }
      p = end + 1;
   }
   while (nlines > 1 && lines[nlines - 1].len == 0) nlines--;   /* blank end */
   return 0;
}

static int max_scroll(void)
{
   int m = nlines * LINE_H - AREA_H;
   return m > 0 ? m : 0;
}

static void clamp(void)
{
   if (scroll_px > max_scroll()) scroll_px = max_scroll();
   if (scroll_px < 0) scroll_px = 0;
}

void guide_close(void)
{
   guide_save_pos();
   free(text); text = NULL;
   free(lines); lines = NULL;
   nlines = 0; scroll_px = 0; t_mode = 0;
   fling = 0; drawn_scroll = -1;
   guide_shown = false;
   g_err = NULL;
   g_file[0] = g_pos[0] = 0;
}

void guide_save_pos(void)
{
   FILE *f;
   if (!text || !g_pos[0]) return;
   f = fopen(g_pos, "w");
   if (!f) return;
   fprintf(f, "line=%d\nshown=%d\n", scroll_px / LINE_H, guide_shown ? 1 : 0);
   fclose(f);
}

static bool exists(const char *p)
{
   struct stat st;
   return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

bool guide_open(const char *rom_path, const char *rom_name, const char *app_dir)
{
   const char *slash = strrchr(rom_path, '/');
   int dl = slash ? (int)(slash - rom_path + 1) : 0;
   FILE *f;
   long size;
   char gdir[520];

   guide_close();
   snprintf(gdir, sizeof(gdir), "%s/guides", app_dir);
   snprintf(g_file, sizeof(g_file), "%.*s%s.txt", dl, rom_path, rom_name);
   if (!exists(g_file))
      snprintf(g_file, sizeof(g_file), "%s/%s.txt", gdir, rom_name);
   if (!exists(g_file)) { g_file[0] = 0; return false; }

   f = fopen(g_file, "rb");
   if (!f) { g_err = "The guide could not be opened."; return false; }
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);
   if (size < 0 || size > GUIDE_MAX)
   {
      fclose(f);
      g_err = "The guide is too big (over 3 MB).";
      return false;
   }
   text = malloc(size + 1);
   if (!text) { fclose(f); g_err = "Not enough memory for the guide."; return false; }
   if (fread(text, 1, size, f) != (size_t)size)
   {
      fclose(f); free(text); text = NULL;
      g_err = "The guide could not be read.";
      return false;
   }
   fclose(f);
   if (wrap(clean(text, (uint32_t)size)))
   {
      free(text); text = NULL; free(lines); lines = NULL; nlines = 0;
      g_err = "Not enough memory for the guide.";
      return false;
   }

   /* where it was left */
   mkdir(gdir, 0777);
   snprintf(g_pos, sizeof(g_pos), "%s/%s.pos", gdir, rom_name);
   f = fopen(g_pos, "r");
   if (f)
   {
      int line = 0, shown = 0;
      if (fscanf(f, "line=%d\nshown=%d", &line, &shown) >= 1)
      {
         scroll_px = line * LINE_H;
         guide_shown = shown != 0;
      }
      fclose(f);
      clamp();
   }
   return true;
}

static void button(int x, int y, int w, int h, const char *label, int arrow, bool on)
{
   ui_rrect(x, y, w, h, h / 2, on ? C_ACCENT : C_PANEL);
   if (arrow)
   {
      int tw = ui_text_w(&ui_font_small, label);
      int x0 = x + (w - tw - 14) / 2;
      ui_arrow(x0 + 4, y + h / 2, 4, arrow, on ? C_TEXT : C_DIM);
      ui_text_vc(&ui_font_small, x0 + 14, y, h, label, on ? C_TEXT : C_DIM, 0);
   }
   else
      ui_text_center(&ui_font_bold, x + w / 2, y + (h - ui_font_bold.height) / 2, label, C_TEXT);
}

void guide_invalidate(void) { drawn_scroll = -1; }

/* the text lines that cover rows y0 <= y < y1 of the text area */
static void draw_text_rows(int y0, int y1)
{
   char buf[160];
   int i, y;
   ui_fill(0, y0, BAR_X + 6, y1 - y0, C_BG);
   ui_set_clip_y(y0, y1);
   i = scroll_px / LINE_H;
   y = AREA_Y - scroll_px % LINE_H;
   for (; i < nlines && y < y1; i++, y += LINE_H)
   {
      int len;
      if (y + LINE_H <= y0) continue;
      len = lines[i].len < (int)sizeof(buf) - 1 ? lines[i].len : (int)sizeof(buf) - 1;
      memcpy(buf, text + lines[i].off, len);
      buf[len] = 0;
      ui_text(&ui_font_small, TEXT_X, y, buf, C_TEXT, 0);
   }
   ui_set_clip_y(0, UI_H);
}

void guide_draw(void)
{
   char buf[160];
   int first, d = drawn_scroll >= 0 ? scroll_px - drawn_scroll : AREA_H, ms = max_scroll();

   if (!text) { ui_clear(C_BG); return; }

   /* text: a small scroll moves what's already drawn and draws only the rows that
      came into view, so dragging stays smooth */
   if (d > -AREA_H / 2 && d < AREA_H / 2)
   {
      int y;
      if (d > 0)
      {
         for (y = AREA_Y; y < AREA_Y + AREA_H - d; y++)
            memcpy(&ui_canvas[y * UI_W], &ui_canvas[(y + d) * UI_W], (BAR_X + 6) * 2);
         draw_text_rows(AREA_Y + AREA_H - d, AREA_Y + AREA_H);
      }
      else if (d < 0)
      {
         for (y = AREA_Y + AREA_H - 1; y >= AREA_Y - d; y--)
            memcpy(&ui_canvas[y * UI_W], &ui_canvas[(y + d) * UI_W], (BAR_X + 6) * 2);
         draw_text_rows(AREA_Y, AREA_Y - d);
      }
   }
   else
   {
      ui_clear(C_BG);
      draw_text_rows(AREA_Y, AREA_Y + AREA_H);
   }
   drawn_scroll = scroll_px;
   first = scroll_px / LINE_H;

   /* scroll bar */
   ui_fill(BAR_X + 6, AREA_Y, UI_W - BAR_X - 6, AREA_H, C_BG);
   ui_rrect(BAR_X + 8, AREA_Y, 6, AREA_H, 3, C_PANEL);
   if (ms > 0)
   {
      int th = AREA_H * AREA_H / (nlines * LINE_H);
      if (th < 18) th = 18;
      ui_rrect(BAR_X + 8, AREA_Y + (AREA_H - th) * scroll_px / ms, 6, th, 3,
               t_mode == 2 ? C_ACCENT : C_TRACK);
   }

   /* header (over any text that scrolled under it) */
   ui_fill(0, 0, UI_W, AREA_Y, C_BG);
   ui_vgradient(0, 0, UI_W, HEAD_H, C_BG2, C_BG);
   ui_text_vc(&ui_font_bold, 10, 0, HEAD_H, "Guide", C_TEXT, 0);
   snprintf(buf, sizeof(buf), "%d%%", ms ? scroll_px * 100 / ms : 100);
   ui_text_vc(&ui_font_small, 62, 0, HEAD_H, buf, C_DIM, 0);
   button(236, 4, 76, 22, "Menu", 0, true);

   /* footer */
   ui_fill(0, FOOT_Y - 2, UI_W, UI_H - FOOT_Y + 2, C_BG);
   ui_fill(0, FOOT_Y - 2, UI_W, 1, C_PANEL);
   button(8, FOOT_Y + 6, 96, 24, "Page up", 2, scroll_px > 0);
   button(216, FOOT_Y + 6, 96, 24, "Page down", -2, scroll_px < ms);
   snprintf(buf, sizeof(buf), "line %d / %d", first + 1, nlines);
   ui_text_center(&ui_font_small, UI_W / 2, FOOT_Y + 11, buf, C_FAINT);
}

static void bar_to(int y)
{
   int ms = max_scroll();
   int th = nlines ? AREA_H * AREA_H / (nlines * LINE_H) : AREA_H;
   if (th < 18) th = 18;
   if (AREA_H <= th) return;
   scroll_px = (int)((long long)(y - AREA_Y - th / 2) * ms / (AREA_H - th));
   clamp();
}

int guide_touch(int phase, int x, int y)
{
   int before = scroll_px, was = t_mode;
   if (!text) return 0;
   if (phase == GUIDE_TOUCH_DOWN)
   {
      t_mode = 0;
      fling = 0;
      t_vel = 0;
      if (y < HEAD_H && x >= 224)
         return GUIDE_MENU;
      if (y >= FOOT_Y)
      {
         int page = (AREA_H / LINE_H - 1) * LINE_H;
         if (x < 120) scroll_px -= page;
         else if (x >= 200) scroll_px += page;
         scroll_px -= scroll_px % LINE_H;   /* whole lines at the top */
         clamp();
      }
      else if (y >= HEAD_H)
      {
         if (x >= BAR_X) { t_mode = 2; bar_to(y); }
         else { t_mode = 1; t_last_y = y; }
      }
   }
   else if (phase == GUIDE_TOUCH_HELD)
   {
      if (t_mode == 1)
      {
         int dy = t_last_y - y;
         scroll_px += dy;
         t_vel = t_vel * 0.5f + dy * 0.5f;
         t_last_y = y;
         clamp();
      }
      else if (t_mode == 2) bar_to(y);
   }
   else
   {
      if (t_mode == 1 && (t_vel > 1.5f || t_vel < -1.5f))
         fling = t_vel;   /* a flick keeps scrolling and slows down */
      t_mode = 0;
   }
   return scroll_px != before || t_mode != was ? GUIDE_REDRAW : 0;
}

int guide_tick(void)
{
   int before = scroll_px;
   if (!text || fling == 0)
      return 0;
   scroll_px += (int)(fling + (fling > 0 ? 0.5f : -0.5f));
   fling *= 0.94f;
   if (fling > -0.5f && fling < 0.5f)
      fling = 0;
   clamp();
   if (scroll_px == before)
      fling = 0;   /* reached the top or the end */
   return scroll_px != before ? GUIDE_REDRAW : 0;
}
