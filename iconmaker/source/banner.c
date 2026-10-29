/* The HOME Menu banner (the picture on the top screen): put a new 256x128
 * picture into a banner made by bannertool, in place.
 *
 * A banner (CBMD) holds an LZ11-compressed CGFX model and a CWAV sound.
 * bannertool's model has one 256x128 texture, "COMMON1", in RGBA4444,
 * stored in 8x8 tiles, Z-order inside a tile, top row first. The new
 * picture is compressed again and must fit in the room the old one had,
 * before the sound: the template's banner is made from a noise picture, which
 * hardly compresses, so there's always room. Formats from 3dbrew.org (CBMD,
 * CGFX, LZ11).
 *
 * The 3D template (tools/make_banner3d.py, through pycgfx) has two RGBA4
 * textures instead: the 128x128 icon on the turning tile, and the 256x64
 * name strip under it. pycgfx stores rows bottom first, so those are
 * written flipped. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "banner.h"

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* --- LZ11 --- */

static int lz11_decompress(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t outlen)
{
   uint32_t i = 4, o = 0;
   if (inlen < 4 || in[0] != 0x11 || (rd32(in) >> 8) != outlen)
      return -1;
   while (o < outlen)
   {
      int b;
      uint8_t flags;
      if (i >= inlen)
         return -1;
      flags = in[i++];
      for (b = 0; b < 8 && o < outlen; b++)
      {
         if (flags & (0x80 >> b))
         {
            uint32_t len, dist, t;
            if (i + 1 >= inlen)
               return -1;
            t = in[i] >> 4;
            if (t == 0)
            {
               if (i + 2 >= inlen) return -1;
               len = (((in[i] & 15) << 4) | (in[i + 1] >> 4)) + 0x11;
               dist = (((in[i + 1] & 15) << 8) | in[i + 2]) + 1;
               i += 3;
            }
            else if (t == 1)
            {
               if (i + 3 >= inlen) return -1;
               len = (((in[i] & 15) << 12) | (in[i + 1] << 4) | (in[i + 2] >> 4)) + 0x111;
               dist = (((in[i + 2] & 15) << 8) | in[i + 3]) + 1;
               i += 4;
            }
            else
            {
               len = t + 1;
               dist = (((in[i] & 15) << 8) | in[i + 1]) + 1;
               i += 2;
            }
            if (dist > o || len > outlen - o)
               return -1;
            while (len--)
            {
               out[o] = out[o - dist];
               o++;
            }
         }
         else
         {
            if (i >= inlen)
               return -1;
            out[o++] = in[i++];
         }
      }
   }
   return 0;
}

#define LZ_WINDOW 4096
#define LZ_MAXLEN (0xFFFF + 0x111)
#define LZ_HASH   4096
#define LZ_CHAIN  64

/* Greedy LZ11 with hash chains. Returns the compressed size, or 0 if it
 * does not fit in outmax. */
static uint32_t lz11_compress(const uint8_t *in, uint32_t len, uint8_t *out, uint32_t outmax)
{
   int32_t *head = malloc(LZ_HASH * sizeof(int32_t)), *prev = malloc(len * sizeof(int32_t));
   uint32_t i = 0, o = 4, flagpos = 0;
   int bit = 8;
   if (!head || !prev || outmax < 4)
   {
      free(head);
      free(prev);
      return 0;
   }
   memset(head, 0xFF, LZ_HASH * sizeof(int32_t));
   out[0] = 0x11;
   out[1] = len;
   out[2] = len >> 8;
   out[3] = len >> 16;

#define HASH(p) ((((p)[0] << 8) ^ ((p)[1] << 4) ^ (p)[2]) & (LZ_HASH - 1))
#define INSERT(p) do { if ((p) + 2 < len) { uint32_t h_ = HASH(in + (p)); \
                       prev[p] = head[h_]; head[h_] = (int32_t)(p); } } while (0)
   while (i < len)
   {
      uint32_t best = 0, bestdist = 0, k;
      if (bit == 8)
      {
         if (o >= outmax) goto full;
         flagpos = o++;
         out[flagpos] = 0;
         bit = 0;
      }
      if (i + 2 < len)
      {
         int32_t c = head[HASH(in + i)];
         int n = 0;
         uint32_t maxlen = len - i < LZ_MAXLEN ? len - i : LZ_MAXLEN;
         while (c >= 0 && i - (uint32_t)c <= LZ_WINDOW && n++ < LZ_CHAIN)
         {
            uint32_t l = 0;
            while (l < maxlen && in[c + l] == in[i + l])
               l++;
            if (l > best)
            {
               best = l;
               bestdist = i - c;
               if (l == maxlen)
                  break;
            }
            c = prev[c];
         }
      }
      if (best >= 3)
      {
         uint32_t d = bestdist - 1;
         if (o + 4 > outmax) goto full;
         out[flagpos] |= 0x80 >> bit;
         if (best <= 0x10)
         {
            out[o++] = (uint8_t)(((best - 1) << 4) | (d >> 8));
            out[o++] = (uint8_t)d;
         }
         else if (best <= 0x110)
         {
            uint32_t l = best - 0x11;
            out[o++] = (uint8_t)(l >> 4);
            out[o++] = (uint8_t)(((l & 15) << 4) | (d >> 8));
            out[o++] = (uint8_t)d;
         }
         else
         {
            uint32_t l = best - 0x111;
            out[o++] = (uint8_t)(0x10 | (l >> 12));
            out[o++] = (uint8_t)(l >> 4);
            out[o++] = (uint8_t)(((l & 15) << 4) | (d >> 8));
            out[o++] = (uint8_t)d;
         }
         for (k = 0; k < best; k++)
            INSERT(i + k);
         i += best;
      }
      else
      {
         if (o >= outmax) goto full;
         out[o++] = in[i];
         INSERT(i);
         i++;
      }
      bit++;
   }
#undef HASH
#undef INSERT
   free(head);
   free(prev);
   return o;
full:
   free(head);
   free(prev);
   return 0;
}

/* --- the texture --- */

static int zorder(int x, int y)
{
   int v = 0, b;
   for (b = 0; b < 3; b++)
      v |= ((x >> b) & 1) << (2 * b) | ((y >> b) & 1) << (2 * b + 1);
   return v;
}

/* RGBA8 to RGBA4444, with a little ordered dither so gradients don't band */
static void encode_rgba4(const uint8_t *rgba, int w, int h, uint8_t *tex, int flip)
{
   static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
   int x, y, k;
   for (y = 0; y < h; y++)
      for (x = 0; x < w; x++)
      {
         const uint8_t *p = rgba + ((flip ? h - 1 - y : y) * w + x) * 4;
         int t = ((y / 8) * (w / 8) + x / 8) * 64 + zorder(x & 7, y & 7);
         int d = bayer[y & 3][x & 3];
         uint16_t v = 0;
         for (k = 0; k < 4; k++)
         {
            int c = k < 3 ? (p[k] * 15 + d * 16) / 255 : (p[3] * 15 + 127) / 255;
            if (c > 15) c = 15;
            v |= c << (12 - 4 * k);
         }
         tex[t * 2] = (uint8_t)v;
         tex[t * 2 + 1] = (uint8_t)(v >> 8);
      }
}

static int fail(char *err, int errlen, const char *msg)
{
   if (err && errlen > 0)
      snprintf(err, errlen, "banner: %s", msg);
   return -1;
}

/* the offset of the RGBA4 texture object of w x h in the model, or 0 */
static uint32_t find_txob(const uint8_t *cgfx, uint32_t glen, uint32_t w, uint32_t h)
{
   uint32_t i;
   for (i = 0; i + 0x48 <= glen; i += 4)
      if (!memcmp(cgfx + i, "TXOB", 4) && rd32(cgfx + i + 0x14) == h &&
          rd32(cgfx + i + 0x18) == w && rd32(cgfx + i + 0x30) == 4 &&
          rd32(cgfx + i + 0x40) == w * h * 2)
         return i;
   return 0;
}

static int put_texture(uint8_t *cgfx, uint32_t glen, uint32_t txob, int w, int h,
                       const uint8_t *rgba, int flip)
{
   uint32_t n = txob + 0x44 + rd32(cgfx + txob + 0x44);  /* data offset is relative */
   if (n < txob || n > glen || (uint32_t)(w * h * 2) > glen - n)
      return -1;
   encode_rgba4(rgba, w, h, cgfx + n, flip);
   return 0;
}

int banner_set_image(uint8_t *cbmd, uint32_t size, const uint8_t *rgba256x128,
                     char *err, int errlen)
{
   return banner_set_images(cbmd, size, rgba256x128, NULL, err, errlen);
}

int banner_set_images(uint8_t *cbmd, uint32_t size, const uint8_t *rgba256x128,
                      const uint8_t *label128, char *err, int errlen)
{
   uint32_t cgfx_off, cwav_off, room, glen, i, n, txob, label;
   uint8_t *cgfx, *packed;
   int ret = -1;

   if (size < 0x88 || memcmp(cbmd, "CBMD", 4))
      return fail(err, errlen, "not a CBMD");
   cgfx_off = rd32(cbmd + 8);                  /* the common model */
   cwav_off = rd32(cbmd + 0x84);
   if (cwav_off > size || cgfx_off < 0x88 || cgfx_off + 8 > cwav_off)
      return fail(err, errlen, "bad CBMD offsets");
   for (i = 1; i < 13; i++)                    /* per-region models: none */
      if (rd32(cbmd + 8 + 4 * i))
         return fail(err, errlen, "has region models");
   room = cwav_off - cgfx_off;
   glen = rd32(cbmd + cgfx_off) >> 8;
   if (cbmd[cgfx_off] != 0x11 || glen < 0x100 || glen > 0x80000)
      return fail(err, errlen, "model not LZ11");

   cgfx = malloc(glen);
   packed = malloc(room);
   if (!cgfx || !packed)
   {
      fail(err, errlen, "out of memory");
      goto out;
   }
   if (lz11_decompress(cbmd + cgfx_off, room, cgfx, glen) || memcmp(cgfx, "CGFX", 4))
   {
      fail(err, errlen, "bad model data");
      goto out;
   }

   label = find_txob(cgfx, glen, 128, 128);    /* only in the 3D template */
   txob = label ? find_txob(cgfx, glen, 256, 64) : find_txob(cgfx, glen, 256, 128);
   if (!txob)
   {
      fail(err, errlen, "no banner texture");
      goto out;
   }
   if (put_texture(cgfx, glen, txob, 256, label ? 64 : 128, rgba256x128, label != 0) ||
       (label && label128 && put_texture(cgfx, glen, label, 128, 128, label128, 1)))
   {
      fail(err, errlen, "bad texture offset");
      goto out;
   }

   n = lz11_compress(cgfx, glen, packed, room);
   if (!n)
   {
      fail(err, errlen, "picture too detailed to fit");
      goto out;
   }
   memcpy(cbmd + cgfx_off, packed, n);
   memset(cbmd + cgfx_off + n, 0, room - n);
   ret = 0;
out:
   free(cgfx);
   free(packed);
   return ret;
}
