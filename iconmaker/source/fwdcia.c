/* Forwarder CIA from a template: see fwdcia.h. Formats from 3dbrew.org
 * (CIA, Ticket, Title metadata, NCCH, ExeFS, SMDH). */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "fwdcia.h"
#include "banner.h"

#define SLOT_MAGIC  "GP3D-FWD-SLOT-1"
#define SLOT_TARGET 16
#define SLOT_LAUNCH 24
#define SLOT_PATH   152
#define SLOT_SIZE   664
#define MEDIA_UNIT  0x200

static int fail(char *err, int errlen, const char *fmt, ...)
{
   va_list ap;
   if (err && errlen > 0)
   {
      va_start(ap, fmt);
      vsnprintf(err, errlen, fmt, ap);
      va_end(ap);
   }
   return -1;
}

static uint32_t rd32le(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t rd32be(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static uint16_t rd16be(const uint8_t *p) { return p[0] << 8 | p[1]; }
static uint64_t rd64be(const uint8_t *p) { return (uint64_t)rd32be(p) << 32 | rd32be(p + 4); }
static void wr64le(uint8_t *p, uint64_t v) { int i; for (i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void wr64be(uint8_t *p, uint64_t v) { int i; for (i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i)); }
static uint32_t align64(uint32_t v) { return (v + 63) & ~63u; }

/* size of a signature block (type, signature, padding) by its type */
static uint32_t sig_block_size(uint32_t type)
{
   switch (type)
   {
      case 0x10000: case 0x10003: return 4 + 0x200 + 0x3C;   /* RSA-4096 */
      case 0x10001: case 0x10004: return 4 + 0x100 + 0x3C;   /* RSA-2048 */
      case 0x10002: case 0x10005: return 4 + 0x3C + 0x40;    /* ECDSA */
   }
   return 0;
}

static int in(uint32_t off, uint32_t len, uint32_t size)
{
   return off <= size && len <= size - off;
}

int fwdcia_make(uint8_t *cia, uint32_t size, const fwdcia_params *p,
                fwdcia_sha256_fn sha256, char *err, int errlen)
{
   uint32_t hdrsize, certsize, ticketsize, tmdsize, metasize;
   uint32_t certs, ticket, tmd, content, meta, tmdh, sigsz;
   uint64_t contentsize, c0size;
   uint8_t *ncch, *exefs, *chunk;
   uint32_t ncchsize, exefs_off, exefs_size, exefs_hashsize, i, n, found;
   uint16_t count;
   int code = -1, icon = -1, bnr = -1;
   uint8_t le[8], be[8], nle[8], nbe[8];

   /* --- CIA header and its sections (each aligned to 64 bytes) --- */
   if (size < 0x2040)
      return fail(err, errlen, "template too small");
   hdrsize = rd32le(cia);
   certsize = rd32le(cia + 0x08);
   ticketsize = rd32le(cia + 0x0C);
   tmdsize = rd32le(cia + 0x10);
   metasize = rd32le(cia + 0x14);
   contentsize = (uint64_t)rd32le(cia + 0x18) | (uint64_t)rd32le(cia + 0x1C) << 32;
   if (hdrsize != 0x2020)
      return fail(err, errlen, "not a CIA (header size %X)", (unsigned)hdrsize);
   certs = align64(hdrsize);
   ticket = align64(certs + certsize);
   tmd = align64(ticket + ticketsize);
   content = align64(tmd + tmdsize);
   meta = align64(content + (uint32_t)contentsize);
   if (contentsize > size || !in(content, (uint32_t)contentsize, size) ||
       (metasize && !in(meta, metasize, size)))
      return fail(err, errlen, "CIA sections run past the end");

   /* --- title metadata: content 0 --- */
   sigsz = sig_block_size(rd32be(cia + tmd));
   if (!sigsz || !in(tmd, sigsz + 0xC4 + 0x900 + 0x30, size))
      return fail(err, errlen, "unknown TMD signature type");
   tmdh = tmd + sigsz;
   count = rd16be(cia + tmdh + 0x9E);
   if (count < 1 || !in(tmdh + 0xC4 + 0x900, count * 0x30u, size))
      return fail(err, errlen, "bad TMD content count");
   chunk = cia + tmdh + 0xC4 + 0x900;          /* content chunk record 0 */
   if (rd16be(chunk + 6) & 1)
      return fail(err, errlen, "template content is encrypted");
   c0size = rd64be(chunk + 8);
   if (c0size > contentsize)
      return fail(err, errlen, "bad content size");

   /* --- NCCH (content 0) --- */
   ncch = cia + content;
   ncchsize = (uint32_t)c0size;
   if (ncchsize < 0x600 || memcmp(ncch + 0x100, "NCCH", 4))
      return fail(err, errlen, "content 0 is not an NCCH");
   if (!(ncch[0x18F] & 0x04))
      return fail(err, errlen, "template NCCH is encrypted");
   exefs_off = rd32le(ncch + 0x1A0) * MEDIA_UNIT;
   exefs_size = rd32le(ncch + 0x1A4) * MEDIA_UNIT;
   exefs_hashsize = rd32le(ncch + 0x1A8) * MEDIA_UNIT;
   if (!exefs_off || !in(exefs_off, exefs_size, ncchsize) ||
       exefs_hashsize < 0x200 || exefs_hashsize > exefs_size)
      return fail(err, errlen, "bad ExeFS");
   exefs = ncch + exefs_off;

   /* --- the new title ID, everywhere the template's is --- */
   wr64le(le, FWDCIA_TEMPLATE_TID);
   wr64be(be, FWDCIA_TEMPLATE_TID);
   wr64le(nle, p->title_id);
   wr64be(nbe, p->title_id);
   found = 0;
   for (i = 0; i + 8 <= size; i++)
   {
      if (cia[i] == le[0] && !memcmp(cia + i, le, 8))
      {
         memcpy(cia + i, nle, 8);
         found++;
      }
      else if (cia[i] == be[0] && !memcmp(cia + i, be, 8))
      {
         memcpy(cia + i, nbe, 8);
         found++;
      }
   }
   /* at least ticket, TMD, NCCH program and partition ID, exheader */
   if (found < 5)
      return fail(err, errlen, "template title ID found only %u times", (unsigned)found);

   /* --- ExeFS files --- */
   for (n = 0; n < 10; n++)
   {
      const uint8_t *e = exefs + n * 16;
      if (!e[0])
         continue;
      if (!memcmp(e, ".code\0\0\0", 8))
         code = n;
      else if (!memcmp(e, "icon\0\0\0\0", 8))
         icon = n;
      else if (!memcmp(e, "banner\0\0", 8))
         bnr = n;
   }
   if (code < 0)
      return fail(err, errlen, "no .code in the ExeFS");

   /* the slot in .code */
   {
      uint32_t off = 0x200 + rd32le(exefs + code * 16 + 8);
      uint32_t len = rd32le(exefs + code * 16 + 12);
      uint8_t *data = exefs + off, *slot = NULL;
      if (!in(off, len, exefs_size))
         return fail(err, errlen, "bad .code entry");
      for (i = 0; i + SLOT_SIZE <= len; i++)
         if (data[i] == 'G' && !memcmp(data + i, SLOT_MAGIC, sizeof(SLOT_MAGIC)))
         {
            slot = data + i;
            break;
         }
      if (!slot)
         return fail(err, errlen, "no forwarder slot in .code (is .code compressed?)");
      if (strlen(p->path) >= 512)
         return fail(err, errlen, "game path too long");
      if (p->launch && strlen(p->launch) >= 128)
         return fail(err, errlen, "launch file path too long");
      wr64le(slot + SLOT_TARGET, p->target);
      memset(slot + SLOT_LAUNCH, 0, 128);
      if (p->launch)
         strcpy((char*)slot + SLOT_LAUNCH, p->launch);
      memset(slot + SLOT_PATH, 0, 512);
      strcpy((char*)slot + SLOT_PATH, p->path);
      sha256(data, len, exefs + 0x200 - (code + 1) * 0x20);
   }

   /* the icon */
   if (p->smdh)
   {
      uint32_t off, len;
      if (icon < 0)
         return fail(err, errlen, "no icon in the ExeFS");
      off = 0x200 + rd32le(exefs + icon * 16 + 8);
      len = rd32le(exefs + icon * 16 + 12);
      if (len != FWDCIA_SMDH_SIZE || !in(off, len, exefs_size))
         return fail(err, errlen, "bad icon entry");
      memcpy(exefs + off, p->smdh, len);
      sha256(exefs + off, len, exefs + 0x200 - (icon + 1) * 0x20);
      /* the copy in the CIA's meta section (shown by CIA installers) */
      if (metasize >= 0x400 + FWDCIA_SMDH_SIZE &&
          !memcmp(cia + meta + 0x400, "SMDH", 4))
         memcpy(cia + meta + 0x400, p->smdh, FWDCIA_SMDH_SIZE);
   }

   /* the banner */
   if (p->banner)
   {
      uint32_t off, len;
      if (bnr < 0)
         return fail(err, errlen, "no banner in the ExeFS");
      off = 0x200 + rd32le(exefs + bnr * 16 + 8);
      len = rd32le(exefs + bnr * 16 + 12);
      if (!in(off, len, exefs_size))
         return fail(err, errlen, "bad banner entry");
      if (banner_set_images(exefs + off, len, p->banner, p->label, err, errlen))
         return -1;
      sha256(exefs + off, len, exefs + 0x200 - (bnr + 1) * 0x20);
   }

   /* --- hashes, from the inside out --- */
   sha256(exefs, exefs_hashsize, ncch + 0x1C0);         /* ExeFS superblock */
   if (rd32le(ncch + 0x180) == 0x400)
      sha256(ncch + 0x200, 0x400, ncch + 0x160);       /* exheader */
   sha256(ncch, ncchsize, chunk + 0x10);                /* content 0 */
   {
      /* content info record 0 covers the chunk records from index 0 */
      uint8_t *info = cia + tmdh + 0xC4;
      uint16_t icount = rd16be(info + 2);
      if (!icount || icount > count)
         return fail(err, errlen, "bad content info record");
      sha256(chunk, icount * 0x30u, info + 4);
      sha256(info, 0x900, cia + tmdh + 0xA4);
   }
   return 0;
}

uint64_t fwdcia_title_id_for(const char *path)
{
   /* FNV-1a of the path */
   uint32_t h = 2166136261u, uid;
   const unsigned char *s = (const unsigned char*)path;
   while (*s)
      h = (h ^ *s++) * 16777619u;
   uid = 0xC0000 + h % 0x30000;
   return 0x0004000000000000ULL | (uint64_t)uid << 8;
}

/* --- SMDH --- */

/* UTF-8 to UTF-16LE, at most max units (zero filled) */
static void put_utf16(uint8_t *dst, int max, const char *s)
{
   const unsigned char *u = (const unsigned char*)(s ? s : "");
   int n = 0;
   memset(dst, 0, max * 2);
   while (*u && n < max - 1)
   {
      uint32_t c = *u++;
      if (c >= 0xC0 && c < 0xE0 && (u[0] & 0xC0) == 0x80)
         c = (c & 0x1F) << 6 | (*u++ & 0x3F);
      else if (c >= 0xE0 && c < 0xF0 && (u[0] & 0xC0) == 0x80 && (u[1] & 0xC0) == 0x80)
      {
         c = (c & 0x0F) << 12 | (u[0] & 0x3F) << 6 | (u[1] & 0x3F);
         u += 2;
      }
      else if (c >= 0x80)
         c = '?';
      dst[n * 2] = (uint8_t)c;
      dst[n * 2 + 1] = (uint8_t)(c >> 8);
      n++;
   }
}

/* RGBA over white to RGB565 */
static uint16_t to565(const uint8_t *px)
{
   uint32_t a = px[3], r, g, b;
   r = (px[0] * a + 255 * (255 - a)) / 255;
   g = (px[1] * a + 255 * (255 - a)) / 255;
   b = (px[2] * a + 255 * (255 - a)) / 255;
   return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
}

/* icons are 8x8 tiles, left to right, top to bottom; pixels in a tile in
 * Z order */
static void put_icon(uint8_t *dst, const uint8_t *rgba, int size)
{
   int x, y;
   for (y = 0; y < size; y++)
      for (x = 0; x < size; x++)
      {
         int tile = (y / 8) * (size / 8) + x / 8;
         int i = (x & 1) | (y & 1) << 1 | (x & 2) << 1 | (y & 2) << 2 |
                 (x & 4) << 2 | (y & 4) << 3;
         uint16_t c = to565(rgba + (y * size + x) * 4);
         dst[(tile * 64 + i) * 2] = (uint8_t)c;
         dst[(tile * 64 + i) * 2 + 1] = (uint8_t)(c >> 8);
      }
}

void smdh_set(uint8_t *smdh, const char *short_title, const char *long_title,
              const char *publisher, const uint8_t *rgba48)
{
   int lang;
   for (lang = 0; lang < 16; lang++)
   {
      uint8_t *t = smdh + 8 + lang * 0x200;
      put_utf16(t, 0x40, short_title);
      put_utf16(t + 0x80, 0x80, long_title);
      put_utf16(t + 0x180, 0x40, publisher);
   }
   if (rgba48)
   {
      uint8_t small[24 * 24 * 4];
      int x, y, k;
      for (y = 0; y < 24; y++)
         for (x = 0; x < 24; x++)
            for (k = 0; k < 4; k++)
               small[(y * 24 + x) * 4 + k] = (uint8_t)((
                  rgba48[((2 * y) * 48 + 2 * x) * 4 + k] +
                  rgba48[((2 * y) * 48 + 2 * x + 1) * 4 + k] +
                  rgba48[((2 * y + 1) * 48 + 2 * x) * 4 + k] +
                  rgba48[((2 * y + 1) * 48 + 2 * x + 1) * 4 + k] + 2) / 4);
      put_icon(smdh + 0x2040, small, 24);
      put_icon(smdh + 0x24C0, rgba48, 48);
   }
}

/* the w x h part of a picture `stride` pixels wide, scaled to fit ow x oh */
static void fit_part(const uint8_t *rgba, int stride, int w, int h, uint8_t *out, int ow, int oh)
{
   int x, y, fw, fh, ox, oy;
   memset(out, 0, ow * oh * 4);
   if (w <= 0 || h <= 0)
      return;
   /* the biggest size with the picture's shape that fits; centered */
   if ((long)w * oh >= (long)h * ow)
   {
      fw = ow;
      fh = (int)((long)h * ow / w);
   }
   else
   {
      fh = oh;
      fw = (int)((long)w * oh / h);
   }
   if (fw < 1) fw = 1;
   if (fh < 1) fh = 1;
   ox = (ow - fw) / 2;
   oy = (oh - fh) / 2;
   if (fw > w)
   {
      /* scaling up: bilinear, so a 48x48 icon shown big looks smooth */
      for (y = 0; y < fh; y++)
         for (x = 0; x < fw; x++)
         {
            int fx = (int)(((2L * x + 1) * w * 256 / fw - 256) / 2), fy = (int)(((2L * y + 1) * h * 256 / fh - 256) / 2);
            int x0, y0, ax, ay, i, k;
            uint32_t sum[4] = { 0, 0, 0, 0 };
            uint8_t *o = out + ((oy + y) * ow + ox + x) * 4;
            if (fx < 0) fx = 0;
            if (fy < 0) fy = 0;
            x0 = fx >> 8; y0 = fy >> 8; ax = fx & 255; ay = fy & 255;
            for (i = 0; i < 4; i++)
            {
               int sx = x0 + (i & 1), sy = y0 + (i >> 1);
               uint32_t wgt = (uint32_t)((i & 1) ? ax : 256 - ax) * (uint32_t)((i >> 1) ? ay : 256 - ay);
               const uint8_t *px;
               if (sx >= w) sx = w - 1;
               if (sy >= h) sy = h - 1;
               px = rgba + (sy * stride + sx) * 4;
               for (k = 0; k < 3; k++)
                  sum[k] += px[k] * px[3] * (wgt >> 8);
               sum[3] += px[3] * (wgt >> 8);
            }
            for (k = 0; k < 3; k++)
               o[k] = (uint8_t)(sum[3] ? sum[k] / sum[3] : 0);
            o[3] = (uint8_t)(sum[3] >> 8);
         }
      return;
   }
   for (y = 0; y < fh; y++)
      for (x = 0; x < fw; x++)
      {
         /* source area for this pixel */
         int x0 = (int)((long)x * w / fw), x1 = (int)((long)(x + 1) * w / fw);
         int y0 = (int)((long)y * h / fh), y1 = (int)((long)(y + 1) * h / fh), sx, sy, k;
         uint32_t sum[4] = { 0, 0, 0, 0 }, cnt = 0;
         uint8_t *o = out + ((oy + y) * ow + ox + x) * 4;
         if (x1 <= x0) x1 = x0 + 1;
         if (y1 <= y0) y1 = y0 + 1;
         for (sy = y0; sy < y1 && sy < h; sy++)
            for (sx = x0; sx < x1 && sx < w; sx++)
            {
               const uint8_t *px = rgba + (sy * stride + sx) * 4;
               /* weight colour by alpha, so transparent edges don't darken */
               sum[0] += px[0] * px[3];
               sum[1] += px[1] * px[3];
               sum[2] += px[2] * px[3];
               sum[3] += px[3];
               cnt++;
            }
         if (!cnt)
            continue;
         for (k = 0; k < 3; k++)
            o[k] = (uint8_t)(sum[3] ? sum[k] / sum[3] : 0);
         o[3] = (uint8_t)(sum[3] / cnt);
      }
}

void image_fit(const uint8_t *rgba, int w, int h, uint8_t *out, int ow, int oh)
{
   fit_part(rgba, w, w, h, out, ow, oh);
}

void image_fill(const uint8_t *rgba, int w, int h, uint8_t *out, int ow, int oh)
{
   /* the biggest part in the middle with the shape of ow x oh, fitted */
   int cw = w, ch = h;
   if ((long)w * oh > (long)h * ow)
      cw = (int)((long)h * ow / oh);
   else
      ch = (int)((long)w * oh / ow);
   if (cw < 1) cw = 1;
   if (ch < 1) ch = 1;
   fit_part(rgba + ((long)((h - ch) / 2) * w + (w - cw) / 2) * 4, w, cw, ch, out, ow, oh);
}

void icon_scale48(const uint8_t *rgba, int w, int h, uint8_t *out)
{
   image_fit(rgba, w, h, out, 48, 48);
}
