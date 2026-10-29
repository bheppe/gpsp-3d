#include "ui.h"
#include <string.h>

uint16_t ui_canvas[UI_W * UI_H];

static inline uint16_t blend(uint16_t d, uint16_t s, int a /* 0..256 */)
{
   uint32_t dr = d >> 11, dg = (d >> 5) & 63, db = d & 31;
   uint32_t sr = s >> 11, sg = (s >> 5) & 63, sb = s & 31;
   dr += ((int)(sr - dr) * a) >> 8;
   dg += ((int)(sg - dg) * a) >> 8;
   db += ((int)(sb - db) * a) >> 8;
   return (uint16_t)((dr << 11) | (dg << 5) | db);
}

static int clip_y0 = 0, clip_y1 = UI_H;

void ui_set_clip_y(int y0, int y1)
{
   clip_y0 = y0 < 0 ? 0 : y0;
   clip_y1 = y1 > UI_H ? UI_H : y1;
}

static inline void plot(int x, int y, uint16_t c, int a)
{
   uint16_t *p;
   if ((unsigned)x >= UI_W || y < clip_y0 || y >= clip_y1 || a <= 0)
      return;
   p = &ui_canvas[y * UI_W + x];
   *p = a >= 256 ? c : blend(*p, c, a);
}

void ui_clear(uint16_t c)
{
   ui_fill(0, 0, UI_W, UI_H, c);
}

void ui_fill(int x, int y, int w, int h, uint16_t c)
{
   int i, j;
   if (x < 0) { w += x; x = 0; }
   if (y < 0) { h += y; y = 0; }
   if (x + w > UI_W) w = UI_W - x;
   if (y + h > UI_H) h = UI_H - y;
   for (j = 0; j < h; j++)
   {
      uint16_t *p = &ui_canvas[(y + j) * UI_W + x];
      for (i = 0; i < w; i++)
         p[i] = c;
   }
}

void ui_fill_alpha(int x, int y, int w, int h, uint16_t c, int a)
{
   int i, j;
   for (j = 0; j < h; j++)
      for (i = 0; i < w; i++)
         plot(x + i, y + j, c, a);
}

void ui_vgradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom)
{
   int j;
   for (j = 0; j < h; j++)
      ui_fill(x, y + j, w, 1, blend(top, bottom, h > 1 ? j * 256 / (h - 1) : 0));
}

/* coverage (0..256) of pixel (px,py) by a circle of radius r at (cx,cy) */
static int coverage(float px, float py, float cx, float cy, float r)
{
   float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
   float d2 = dx * dx + dy * dy;
   float din = r - 0.7f, dout = r + 0.7f;
   if (d2 <= din * din)
      return 256;
   if (d2 >= dout * dout)
      return 0;
   {
      /* linear ramp over ~1.4 px around the edge */
      float d = 0, lo = din, hi = dout;
      int k;
      for (k = 0; k < 8; k++)   /* tiny sqrt by bisection: avoids libm */
      {
         float mid = (lo + hi) * 0.5f;
         if (mid * mid < d2) lo = mid; else hi = mid;
      }
      d = (lo + hi) * 0.5f;
      return (int)((dout - d) / (dout - din) * 256.0f);
   }
}

void ui_rrect(int x, int y, int w, int h, int r, uint16_t c)
{
   int i, j;
   if (r * 2 > h) r = h / 2;
   if (r * 2 > w) r = w / 2;
   ui_fill(x + r, y, w - 2 * r, h, c);
   ui_fill(x, y + r, r, h - 2 * r, c);
   ui_fill(x + w - r, y + r, r, h - 2 * r, c);
   for (j = 0; j < r; j++)
      for (i = 0; i < r; i++)
      {
         plot(x + i,         y + j,         c, coverage(i, j, r, r, r));
         plot(x + w - 1 - i, y + j,         c, coverage(i, j, r, r, r));
         plot(x + i,         y + h - 1 - j, c, coverage(i, j, r, r, r));
         plot(x + w - 1 - i, y + h - 1 - j, c, coverage(i, j, r, r, r));
      }
}

void ui_rrect_outline(int x, int y, int w, int h, int r, int t, uint16_t c)
{
   /* outer shape minus inner shape, per pixel */
   int i, j;
   if (r * 2 > h) r = h / 2;
   for (j = 0; j < h; j++)
      for (i = 0; i < w; i++)
      {
         int in_t = i < t || j < t || i >= w - t || j >= h - t;
         int cx = i < r ? r : (i >= w - r ? w - r : i), cy = j < r ? r : (j >= h - r ? h - r : j);
         int corner = (i < r || i >= w - r) && (j < r || j >= h - r);
         if (corner)
         {
            float px = i < r ? i : i - (w - 2 * r);
            float py = j < r ? j : j - (h - 2 * r);
            int a_out = coverage(px, py, r, r, r);
            int a_in  = coverage(px, py, r, r, r - t);
            plot(x + i, y + j, c, a_out - a_in);
         }
         else if (in_t)
            plot(x + i, y + j, c, 256);
         (void)cx; (void)cy;
      }
}

void ui_circle(int cx, int cy, int r, uint16_t c)
{
   int i, j;
   for (j = -r - 1; j <= r + 1; j++)
      for (i = -r - 1; i <= r + 1; i++)
         plot(cx + i, cy + j, c, coverage(cx + i, cy + j, cx + 0.5f, cy + 0.5f, r));
}

void ui_arrow(int cx, int cy, int s, int dir, uint16_t c)
{
   /* filled triangle, s = half height */
   int i, j;
   for (j = -s; j <= s; j++)
   {
      int len = s - (j < 0 ? -j : j);
      for (i = 0; i <= len; i++)
      {
         int a = i == len ? 128 : 256;
         if (dir == 1)       plot(cx - s / 2 + i, cy + j, c, a);
         else if (dir == -1) plot(cx + s / 2 - i, cy + j, c, a);
         else if (dir == 2)  plot(cx + j, cy + s / 2 - i, c, a);
         else                plot(cx + j, cy - s / 2 + i, c, a);
      }
   }
}

/* Next character of a UTF-8 string; a byte that isn't valid UTF-8 is taken
 * as Latin-1, so old file names still show. */
unsigned ui_utf8_next(const char **ps)
{
   const unsigned char *s = (const unsigned char *)*ps;
   unsigned c = s[0];
   if (c >= 0xC2 && c <= 0xDF && (s[1] & 0xC0) == 0x80)
   {
      *ps += 2;
      return ((c & 0x1F) << 6) | (s[1] & 0x3F);
   }
   if (c >= 0xE0 && c <= 0xEF && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80)
   {
      *ps += 3;
      return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
   }
   if (c >= 0xF0 && c <= 0xF4 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80)
   {
      *ps += 4;
      return 0xFFFD;
   }
   *ps += 1;
   return c;
}

const ui_glyph *ui_glyph_of(const ui_font *f, unsigned c)
{
   switch (c)
   {
      case 0x2018: case 0x2019: c = '\''; break;
      case 0x201C: case 0x201D: c = '"'; break;
      case 0x2013: case 0x2014: c = '-'; break;
      case 0x2026: c = '.'; break;
      case 0x2122: c = 'T'; break;
      case 0xA0:   c = ' '; break;
   }
   if (c >= 32 && c <= 126)
      return &f->g[c - 32];
   if (c >= 160 && c <= 255)
      return &f->g[95 + c - 160];
   return &f->g['?' - 32];
}

int ui_text_w(const ui_font *f, const char *s)
{
   int w = 0;
   while (*s)
      w += ui_glyph_of(f, ui_utf8_next(&s))->adv;
   return w;
}

static void draw_glyph(const ui_font *f, const ui_glyph *g, int x, int y, uint16_t c)
{
   int i, j, n = 0;
   const unsigned char *b = f->bits + g->off;
   for (j = 0; j < g->h; j++)
      for (i = 0; i < g->w; i++, n++)
      {
         int v = (b[n >> 1] >> ((n & 1) * 4)) & 15;
         if (v)
            plot(x + g->x + i, y + g->y + j, c, v == 15 ? 256 : v * 17);
      }
}

int ui_text(const ui_font *f, int x, int y, const char *s, uint16_t c, int max_w)
{
   int dots = 0, x0 = x;
   if (max_w > 0 && ui_text_w(f, s) > max_w)
      dots = ui_text_w(f, "...");
   while (*s)
   {
      const ui_glyph *g = ui_glyph_of(f, ui_utf8_next(&s));
      if (dots && x + g->adv - x0 > max_w - dots)
      {
         const char *d = "...";
         for (; *d; d++)
         {
            const ui_glyph *gd = &f->g[*d - 32];
            draw_glyph(f, gd, x, y, c);
            x += gd->adv;
         }
         break;
      }
      draw_glyph(f, g, x, y, c);
      x += g->adv;
   }
   return x;
}

void ui_text_center(const ui_font *f, int cx, int y, const char *s, uint16_t c)
{
   ui_text(f, cx - ui_text_w(f, s) / 2, y, s, c, 0);
}

void ui_text_right(const ui_font *f, int rx, int y, const char *s, uint16_t c)
{
   ui_text(f, rx - ui_text_w(f, s), y, s, c, 0);
}

int ui_text_vc(const ui_font *f, int x, int y, int h, const char *s, uint16_t c, int max_w)
{
   /* centre the cap height (about 0.73 of the ascent) in the box */
   int cap = f->ascent * 73 / 100;
   int top = y + (h - cap) / 2 - (f->ascent - cap);
   return ui_text(f, x, top, s, c, max_w);
}

#ifdef __3DS__
#include <3ds.h>
void ui_present(void)
{
   /* bottom framebuffer: 240 x 320 RGB565, column-major, y flipped */
   u16 *fb = (u16*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
   int bx, by, x, y;
   /* transpose in 16x16 tiles so both sides stay in the cache */
   for (by = 0; by < UI_H; by += 16)
      for (bx = 0; bx < UI_W; bx += 16)
         for (x = bx; x < bx + 16; x++)
         {
            u16 *col = fb + x * UI_H + (UI_H - 1 - by);
            const u16 *src = ui_canvas + by * UI_W + x;
            for (y = 0; y < 16; y++, src += UI_W)
               *col-- = *src;
         }
   GSPGPU_FlushDataCache(fb, UI_W * UI_H * 2);
   gfxScreenSwapBuffers(GFX_BOTTOM, false);
}
#else
void ui_present(void) {}
#endif
