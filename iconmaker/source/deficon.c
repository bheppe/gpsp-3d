/* The icon a game gets when it has no PNG: a cartridge with the game's name
 * on its label, in a color picked from the name, so games tell apart. */
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "deficon.h"
#include "fwdcia.h"

/* 3x5 pixel font: 5 rows of 3 bits, top row first, high bit left */
static const struct { char c; uint8_t r[5]; } font[] = {
   {'A',{2,5,7,5,5}}, {'B',{6,5,6,5,6}}, {'C',{3,4,4,4,3}}, {'D',{6,5,5,5,6}},
   {'E',{7,4,6,4,7}}, {'F',{7,4,6,4,4}}, {'G',{3,4,5,5,3}}, {'H',{5,5,7,5,5}},
   {'I',{7,2,2,2,7}}, {'J',{1,1,1,5,2}}, {'K',{5,5,6,5,5}}, {'L',{4,4,4,4,7}},
   {'M',{5,7,7,5,5}}, {'N',{6,5,5,5,5}}, {'O',{2,5,5,5,2}}, {'P',{6,5,6,4,4}},
   {'Q',{2,5,5,6,3}}, {'R',{6,5,6,5,5}}, {'S',{3,4,2,1,6}}, {'T',{7,2,2,2,2}},
   {'U',{5,5,5,5,7}}, {'V',{5,5,5,5,2}}, {'W',{5,5,7,7,5}}, {'X',{5,5,2,5,5}},
   {'Y',{5,5,2,2,2}}, {'Z',{7,1,2,4,7}},
   {'0',{7,5,5,5,7}}, {'1',{2,6,2,2,7}}, {'2',{6,1,2,4,7}}, {'3',{6,1,2,1,6}},
   {'4',{5,5,7,1,1}}, {'5',{7,4,6,1,6}}, {'6',{3,4,7,5,7}}, {'7',{7,1,2,2,2}},
   {'8',{7,5,7,5,7}}, {'9',{7,5,7,1,6}},
   {'!',{2,2,2,0,2}}, {'?',{6,1,2,0,2}}, {'.',{0,0,0,0,2}}, {',',{0,0,0,2,4}},
   {'\'',{2,2,0,0,0}}, {':',{0,2,0,2,0}}, {'&',{2,5,2,5,3}}, {'+',{0,2,7,2,0}},
   {'-',{0,0,7,0,0}},
};

static const uint8_t *glyph(char c)
{
   unsigned i;
   c = toupper((unsigned char)c);
   for (i = 0; i < sizeof(font) / sizeof(font[0]); i++)
      if (font[i].c == c)
         return font[i].r;
   return NULL;
}

/* a canvas: RGBA, w x h */
typedef struct { uint8_t *p; int w, h; } canvas;

static void px(canvas *c, int x, int y, uint32_t rgb)
{
   uint8_t *p;
   if (x < 0 || y < 0 || x >= c->w || y >= c->h)
      return;
   p = c->p + (y * c->w + x) * 4;
   p[0] = rgb >> 16; p[1] = rgb >> 8; p[2] = rgb; p[3] = 255;
}

static void rect(canvas *c, int x0, int y0, int x1, int y1, uint32_t rgb)
{
   int x, y;
   for (y = y0; y <= y1; y++)
      for (x = x0; x <= x1; x++)
         px(c, x, y, rgb);
}

/* hue (0..359) to a fairly dark, saturated color white text reads on */
static uint32_t hue_rgb(unsigned h)
{
   unsigned hi = (h / 60) % 6, f = (h % 60) * 255 / 60;
   unsigned v = 200, m = 60, q = m + (v - m) * (255 - f) / 255, t = m + (v - m) * f / 255;
   unsigned r, g, b;
   switch (hi)
   {
      case 0: r = v; g = t; b = m; break;
      case 1: r = q; g = v; b = m; break;
      case 2: r = m; g = v; b = t; break;
      case 3: r = m; g = q; b = v; break;
      case 4: r = t; g = m; b = v; break;
      default: r = v; g = m; b = q; break;
   }
   /* darken light hues (yellow, green, cyan) so white text stays readable */
   {
      unsigned lum = (r * 299 + g * 587 + b * 114) / 1000;
      if (lum > 120)
      {
         r = r * 120 / lum; g = g * 120 / lum; b = b * 120 / lum;
      }
   }
   return r << 16 | g << 8 | b;
}

/* the name without "(USA)", "[!]" and the like, and with '_' as spaces */
void clean_name(const char *in, char *out, int n)
{
   int depth = 0, k = 0;
   for (; *in && k < n - 1; in++)
   {
      if (*in == '(' || *in == '[') { depth++; continue; }
      if (*in == ')' || *in == ']') { if (depth) depth--; continue; }
      if (depth)
         continue;
      out[k++] = *in == '_' ? ' ' : *in;
   }
   while (k && out[k - 1] == ' ')
      k--;
   out[k] = 0;
}

/* Word wrap a name into at most rows lines of cols characters; " - "
 * between parts is left out. Returns the number of lines. */
static int wrap(const char *s, char lines[][33], int rows, int cols)
{
   int nl = 0;
   memset(lines, 0, rows * 33);
   while (*s && nl < rows)
   {
      int len = 0, w;
      while (*s == ' ') s++;
      while (*s)
      {
         const char *e = s;
         while (*e && *e != ' ') e++;
         w = (int)(e - s);
         if (w == 1 && *s == '-')
         {
            s = e;
            while (*s == ' ') s++;
            continue;
         }
         if (len && len + 1 + w > cols)
            break;
         if (len)
            lines[nl][len++] = ' ';
         if (w > cols - len)
            w = cols - len;    /* a word longer than a line: cut it */
         memcpy(&lines[nl][len], s, w);
         len += w;
         s = e;
         while (*s == ' ') s++;
         if (len >= cols)
            break;
      }
      if (len)
         nl++;
      else
         break;
   }
   return nl;
}

/* text at (x, y), each font pixel scale x scale, with a dark shadow */
static void text(canvas *c, int x, int y, int scale, const char *str, uint32_t rgb)
{
   int i, gx, gy;
   for (i = 0; str[i]; i++)
   {
      const uint8_t *g = glyph(str[i]);
      if (!g)
         continue;
      for (gy = 0; gy < 5; gy++)
         for (gx = 0; gx < 3; gx++)
            if (g[gy] & (4 >> gx))
            {
               int x0 = x + (i * 4 + gx) * scale, y0 = y + gy * scale;
               rect(c, x0 + scale / 2 + 1, y0 + scale / 2 + 1,
                    x0 + scale + scale / 2, y0 + scale + scale / 2, 0x141420);
            }
      for (gy = 0; gy < 5; gy++)
         for (gx = 0; gx < 3; gx++)
            if (g[gy] & (4 >> gx))
            {
               int x0 = x + (i * 4 + gx) * scale, y0 = y + gy * scale;
               rect(c, x0, y0, x0 + scale - 1, y0 + scale - 1, rgb);
            }
   }
}

static uint32_t name_hue(const char *clean)
{
   uint32_t h = 2166136261u;
   for (; *clean; clean++)
      h = (h ^ (uint8_t)toupper((unsigned char)*clean)) * 16777619u;
   return h % 360;
}

#define LBL_X0 6
#define LBL_X1 41
#define LBL_Y0 13
#define LBL_Y1 42

void default_icon_style(const char *name, int style, uint8_t *rgba48)
{
   canvas c = { rgba48, 48, 48 };
   char clean[128], lines[4][33];
   int nl, i, x, y, lx0, lx1, ly0, ly1, cols;

   memset(rgba48, 0, 48 * 48 * 4);
   clean_name(name, clean, sizeof(clean));

   if (style == ICON_SNES)
   {
      /* a Super Nintendo cartridge: wide, light grey, ridged lower sides */
      rect(&c, 4, 5, 43, 44, 0xA9ABB5);
      rect(&c, 2, 8, 45, 44, 0xA9ABB5);
      rect(&c, 3, 6, 44, 7, 0xA9ABB5);
      rect(&c, 2, 42, 45, 44, 0x8A8C96);
      for (y = 30; y <= 40; y += 3)
      {
         rect(&c, 3, y, 6, y, 0x7C7E88);
         rect(&c, 41, y, 44, y, 0x7C7E88);
      }
      lx0 = 9; lx1 = 38; ly0 = 9; ly1 = 37;
      cols = 7;
   }
   else
   {
      /* a Game Boy Advance cartridge: body, grip ridges at the top, label */
      rect(&c, 4, 4, 43, 45, 0x4A4E58);
      rect(&c, 3, 6, 44, 45, 0x4A4E58);
      rect(&c, 4, 45, 43, 45, 0x32353D);
      rect(&c, 3, 44, 3, 45, 0x32353D);
      rect(&c, 44, 44, 44, 45, 0x32353D);
      for (x = 10; x <= 36; x += 4)
         rect(&c, x, 6, x + 1, 9, 0x5E6370);
      lx0 = LBL_X0; lx1 = LBL_X1; ly0 = LBL_Y0; ly1 = LBL_Y1;
      cols = 9;
   }
   rect(&c, lx0 - 1, ly0 - 1, lx1 + 1, ly1 + 1, style == ICON_SNES ? 0x4A4C55 : 0x2A2C33);
   rect(&c, lx0, ly0, lx1, ly1, hue_rgb(name_hue(clean)));

   nl = wrap(clean, lines, 4, cols);
   if (!nl)
   {
      strcpy(lines[0], style == ICON_SNES ? "SNES" : "GBA");
      nl = 1;
   }
   for (i = 0; i < nl; i++)
   {
      int len = (int)strlen(lines[i]);
      text(&c, lx0 + (lx1 - lx0 + 1 - (len * 4 - 1)) / 2,
           ly0 + (ly1 - ly0 + 1 - (nl * 7 - 2)) / 2 + i * 7, 1, lines[i], 0xFFFFFF);
   }
}

void default_icon(const char *name, uint8_t *rgba48)
{
   default_icon_style(name, ICON_GBA, rgba48);
}

/* src (sw x sh RGBA) over dst (dw wide) at (x0, y0), by src's alpha */
static void over(uint8_t *dst, int dw, int x0, int y0, const uint8_t *src, int sw, int sh)
{
   int x, y, k;
   for (y = 0; y < sh; y++)
      for (x = 0; x < sw; x++)
      {
         const uint8_t *s = src + (y * sw + x) * 4;
         uint8_t *d = dst + ((y0 + y) * dw + x0 + x) * 4;
         for (k = 0; k < 3; k++)
            d[k] = (s[k] * s[3] + d[k] * (255 - s[3])) / 255;
      }
}

static void background(canvas *c)
{
   int y;
   /* gpSP 3D's background: navy, darker towards the bottom */
   for (y = 0; y < c->h; y++)
   {
      uint32_t r = 42 + (15 - 42) * y / (c->h - 1), g = 35 + (13 - 35) * y / (c->h - 1);
      uint32_t b = 107 + (41 - 107) * y / (c->h - 1);
      rect(c, 0, y, c->w - 1, y, r << 16 | g << 8 | b);
   }
}

void banner_from_image(const uint8_t *img, int w, int h, uint8_t *rgba256x128)
{
   static uint8_t fit[256 * 128 * 4];
   canvas c = { rgba256x128, 256, 128 };
   background(&c);
   image_fit(img, w, h, fit, 256, 128);
   over(rgba256x128, 256, 0, 0, fit, 256, 128);
}

void default_banner(const char *name, const uint8_t *icon, int iw, int ih,
                    uint8_t *rgba256x128, int with_text)
{
   canvas c = { rgba256x128, 256, 128 };
   static uint8_t big[96 * 96 * 4];
   char clean[128], lines[5][33];
   int x, y, nl, i, k, ty;

   background(&c);

   /* the icon, 96x96, over a soft shadow */
   image_fit(icon, iw, ih, big, 96, 96);
   for (y = 0; y < 96; y++)
      for (x = 0; x < 96; x++)
      {
         uint8_t a = big[(y * 96 + x) * 4 + 3];
         uint8_t *sh = rgba256x128 + ((19 + y) * 256 + 15 + x) * 4;
         for (k = 0; k < 3; k++)
            sh[k] = sh[k] * (255 - a / 3) / 255;
      }
   over(rgba256x128, 256, 12, 16, big, 96, 96);

   if (!with_text)
      return;

   /* the name, and "gpSP 3D" under it */
   clean_name(name, clean, sizeof(clean));
   nl = wrap(clean, lines, 5, 15);
   if (!nl)
   {
      strcpy(lines[0], "GBA game");
      nl = 1;
   }
   ty = (128 - (nl * 14 + 18)) / 2;
   for (i = 0; i < nl; i++)
      text(&c, 120, ty + i * 14, 2, lines[i], 0xFFFFFF);
   text(&c, 120, ty + nl * 14 + 6, 2, "GPSP 3D", 0x23D5C8);
}
