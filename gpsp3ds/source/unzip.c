/* Unzip a GBA game for the gpSP core, which can only load a plain ROM file.
 *
 * The first .gba (or .agb / .bin, else the largest file) in a .zip is
 * unpacked to a cache file on the SD card, streaming: deflate needs only a
 * 32 KB window, so a 32 MB ROM unpacks with a few dozen KB of memory. The
 * cache keeps the last game: starting it again reuses the file when its
 * size and CRC-32 note match. Formats from the ZIP application note
 * (PKWARE APPNOTE.TXT) and RFC 1951 (deflate). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "unzip.h"

/* ---------------- CRC-32 ---------------- */

static uint32_t crc_table[256];

static void crc_init(void)
{
   uint32_t i, j, c;
   if (crc_table[1])
      return;
   for (i = 0; i < 256; i++)
   {
      c = i;
      for (j = 0; j < 8; j++)
         c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      crc_table[i] = c;
   }
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
   crc = ~crc;
   while (n--)
      crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
   return ~crc;
}

/* ---------------- streaming inflate (RFC 1951) ---------------- */

#define WSIZE 32768u

typedef struct
{
   FILE *in;
   uint32_t in_left;          /* compressed bytes not yet read */
   uint8_t ibuf[16384];
   uint32_t ipos, ilen;
   uint32_t bitbuf, bitcnt;
   int eof;                   /* bytes read past the end of the input (as zeros):
                               * reading ahead may do that near the end; real
                               * damage shows as a wrong size or CRC */

   FILE *out;
   uint8_t win[WSIZE];        /* the last 32 KB written (back references) */
   uint32_t wpos;             /* next write position in win */
   uint32_t total;            /* bytes written */
   uint32_t limit;            /* expected size: never write more */
   uint32_t crc;
   int werr;
} inf_state;

static int get_byte(inf_state *s)
{
   if (s->ipos == s->ilen)
   {
      uint32_t n = s->in_left < sizeof(s->ibuf) ? s->in_left : sizeof(s->ibuf);
      if (!n || fread(s->ibuf, 1, n, s->in) != n)
      {
         s->in_left = 0;
         s->eof++;
         return 0;
      }
      s->in_left -= n;
      s->ipos = 0;
      s->ilen = n;
   }
   return s->ibuf[s->ipos++];
}

static uint32_t bits(inf_state *s, int need)
{
   uint32_t v;
   while ((int)s->bitcnt < need)
   {
      s->bitbuf |= (uint32_t)get_byte(s) << s->bitcnt;
      s->bitcnt += 8;
   }
   v = s->bitbuf & ((1u << need) - 1);
   s->bitbuf >>= need;
   s->bitcnt -= need;
   return v;
}

static void flush_window(inf_state *s, uint32_t upto)
{
   /* write win[0..upto) (called when the window fills, and at the end) */
   if (!upto)
      return;
   s->crc = crc_update(s->crc, s->win, upto);
   if (fwrite(s->win, 1, upto, s->out) != upto)
      s->werr = 1;
}

static int put(inf_state *s, uint8_t b)
{
   if (s->total >= s->limit)
      return -1;               /* more data than the zip says: corrupt */
   s->win[s->wpos++] = b;
   s->total++;
   if (s->wpos == WSIZE)
   {
      flush_window(s, WSIZE);
      s->wpos = 0;
   }
   return 0;
}

/* canonical Huffman code: counts per length, symbols in code order, and a
 * 9-bit lookup table for the short codes (most symbols) */
#define FASTBITS 9
typedef struct
{
   uint16_t count[16];
   uint16_t symbol[288];
   uint16_t fast[1 << FASTBITS];   /* (length << 12) | symbol, 0 = not short */
} huffman;

static int build(huffman *h, const uint8_t *length, int n)
{
   uint16_t offs[16];
   int left = 1, len, sym, i;
   memset(h->count, 0, sizeof(h->count));
   for (sym = 0; sym < n; sym++)
      h->count[length[sym]]++;
   if (h->count[0] == n)
      return 0;                  /* no codes: allowed (only distance) */
   for (len = 1; len < 16; len++)
   {
      left <<= 1;
      left -= h->count[len];
      if (left < 0)
         return -1;              /* over-subscribed */
   }
   offs[1] = 0;
   for (len = 1; len < 15; len++)
      offs[len + 1] = offs[len] + h->count[len];
   for (sym = 0; sym < n; sym++)
      if (length[sym])
         h->symbol[offs[length[sym]]++] = (uint16_t)sym;

   /* fast table: walk the codes in order, assigning bit-reversed codes */
   memset(h->fast, 0, sizeof(h->fast));
   {
      int code = 0, idx = 0;
      for (len = 1; len <= FASTBITS; len++)
      {
         for (i = 0; i < h->count[len]; i++, idx++, code++)
         {
            /* reverse 'len' bits of code */
            int r = 0, b, c = code, k;
            for (b = 0; b < len; b++)
            {
               r = (r << 1) | (c & 1);
               c >>= 1;
            }
            for (k = r; k < (1 << FASTBITS); k += 1 << len)
               h->fast[k] = (uint16_t)((len << 12) | h->symbol[idx]);
         }
         code <<= 1;
      }
   }
   return left;                  /* > 0: incomplete (allowed for one code) */
}

static int decode(inf_state *s, const huffman *h)
{
   int len, code = 0, first = 0, index = 0, count;
   /* fast path: peek FASTBITS bits if we have them */
   while ((int)s->bitcnt < FASTBITS && s->eof < 4)
   {
      s->bitbuf |= (uint32_t)get_byte(s) << s->bitcnt;
      s->bitcnt += 8;
   }
   if ((int)s->bitcnt >= FASTBITS)
   {
      uint16_t e = h->fast[s->bitbuf & ((1u << FASTBITS) - 1)];
      if (e)
      {
         int l = e >> 12;
         s->bitbuf >>= l;
         s->bitcnt -= l;
         return e & 0xFFF;
      }
   }
   /* slow path, one bit at a time (as in zlib's puff.c) */
   for (len = 1; len < 16; len++)
   {
      code |= (int)bits(s, 1);
      count = h->count[len];
      if (code - count < first)
         return h->symbol[index + (code - first)];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
   }
   return -1;
}

static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
   35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
   3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
   257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
   7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(inf_state *s, const huffman *lit, const huffman *dist)
{
   for (;;)
   {
      int sym = decode(s, lit);
      if (sym < 0 || s->eof > 8)
         return -1;
      if (sym < 256)
      {
         if (put(s, (uint8_t)sym))
            return -1;
      }
      else if (sym == 256)
         return 0;
      else
      {
         uint32_t len, d, from;
         sym -= 257;
         if (sym >= 29)
            return -1;
         len = len_base[sym] + bits(s, len_extra[sym]);
         sym = decode(s, dist);
         if (sym < 0 || sym >= 30)
            return -1;
         d = dist_base[sym] + bits(s, dist_extra[sym]);
         if (d > s->total || d > WSIZE)
            return -1;
         from = (s->wpos - d) & (WSIZE - 1);
         while (len--)
         {
            uint8_t b = s->win[from];
            from = (from + 1) & (WSIZE - 1);
            if (put(s, b))
               return -1;
         }
      }
   }
}

/* a byte at a byte boundary: whole bytes still in the bit buffer (read
 * ahead by decode) come first */
static int aligned_byte(inf_state *s)
{
   if (s->bitcnt >= 8)
   {
      int v = s->bitbuf & 0xFF;
      s->bitbuf >>= 8;
      s->bitcnt -= 8;
      return v;
   }
   return get_byte(s);
}

static int stored(inf_state *s)
{
   uint32_t len, nlen;
   s->bitbuf >>= s->bitcnt & 7;   /* to a byte boundary */
   s->bitcnt &= ~7u;
   len = aligned_byte(s);
   len |= (uint32_t)aligned_byte(s) << 8;
   nlen = aligned_byte(s);
   nlen |= (uint32_t)aligned_byte(s) << 8;
   if (len != (~nlen & 0xFFFF) || s->eof > 8)
      return -1;
   while (len--)
   {
      uint8_t b = (uint8_t)aligned_byte(s);
      if (s->eof > 8 || put(s, b))
         return -1;
   }
   return 0;
}

static int fixed(inf_state *s)
{
   static huffman lit, dist;
   static int built;
   if (!built)
   {
      uint8_t l[288];
      int i;
      for (i = 0; i < 144; i++) l[i] = 8;
      for (; i < 256; i++) l[i] = 9;
      for (; i < 280; i++) l[i] = 7;
      for (; i < 288; i++) l[i] = 8;
      build(&lit, l, 288);
      for (i = 0; i < 30; i++) l[i] = 5;
      build(&dist, l, 30);
      built = 1;
   }
   return codes(s, &lit, &dist);
}

static int dynamic(inf_state *s)
{
   static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
   static huffman lit, dist, lencode;
   uint8_t lengths[320];
   int nlen, ndist, ncode, index, err;

   nlen = bits(s, 5) + 257;
   ndist = bits(s, 5) + 1;
   ncode = bits(s, 4) + 4;
   if (nlen > 286 || ndist > 30)
      return -1;
   memset(lengths, 0, sizeof(lengths));
   for (index = 0; index < ncode; index++)
      lengths[order[index]] = (uint8_t)bits(s, 3);
   if (build(&lencode, lengths, 19) != 0)
      return -1;                 /* must be complete */
   index = 0;
   while (index < nlen + ndist)
   {
      int sym = decode(s, &lencode), len = 0, rep;
      if (sym < 0)
         return -1;
      if (sym < 16)
      {
         lengths[index++] = (uint8_t)sym;
         continue;
      }
      if (sym == 16)
      {
         if (!index)
            return -1;
         len = lengths[index - 1];
         rep = 3 + bits(s, 2);
      }
      else if (sym == 17)
         rep = 3 + bits(s, 3);
      else
         rep = 11 + bits(s, 7);
      if (index + rep > nlen + ndist)
         return -1;
      while (rep--)
         lengths[index++] = (uint8_t)len;
   }
   if (!lengths[256])
      return -1;                 /* no end-of-block code */
   err = build(&lit, lengths, nlen);
   if (err < 0 || (err > 0 && nlen - lit.count[0] != 1))
      return -1;
   err = build(&dist, lengths + nlen, ndist);
   if (err < 0 || (err > 0 && ndist - dist.count[0] != 1))
      return -1;
   return codes(s, &lit, &dist);
}

/* inflate comp bytes from in to out; returns 0 and the CRC-32 */
static int inflate_stream(FILE *in, uint32_t comp, FILE *out, uint32_t size, uint32_t *crc)
{
   inf_state *s = calloc(1, sizeof(inf_state));
   int last, type, err = 0;
   if (!s)
      return -1;
   s->in = in;
   s->in_left = comp;
   s->out = out;
   s->limit = size;
   do
   {
      last = (int)bits(s, 1);
      type = (int)bits(s, 2);
      err = type == 0 ? stored(s) : type == 1 ? fixed(s) : type == 2 ? dynamic(s) : -1;
      if (err || s->eof > 8)
      {
         err = -1;
         break;
      }
   } while (!last);
   if (!err)
   {
      flush_window(s, s->wpos);
      if (s->werr || s->total != size)
         err = -1;
   }
   *crc = s->crc;
   free(s);
   return err;
}

/* ---------------- zip ---------------- */

static uint32_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int fail(char *err, int errlen, const char *msg)
{
   snprintf(err, errlen, "%s", msg);
   return -1;
}

static int is_rom_name(const char *name)
{
   const char *dot = strrchr(name, '.');
   return dot && (!strcasecmp(dot, ".gba") || !strcasecmp(dot, ".agb") || !strcasecmp(dot, ".bin"));
}

/* remove every file in the cache folder except keep (one game is kept) */
static void cache_prune(const char *dir, const char *keep)
{
   DIR *d = opendir(dir);
   struct dirent *e;
   char p[700];
   if (!d)
      return;
   while ((e = readdir(d)))
   {
      if (e->d_name[0] == '.')
         continue;
      snprintf(p, sizeof(p), "%s/%s", dir, e->d_name);
      if (strcmp(p, keep))
         remove(p);
   }
   closedir(d);
}

int unzip_rom(const char *zip_path, const char *cache_dir, const char *name,
              char *out_path, int out_len, unzip_progress_fn progress,
              char *err, int errlen)
{
   FILE *f = fopen(zip_path, "rb"), *o;
   uint8_t buf[512], *cd = NULL;
   long fsize, search;
   uint32_t cd_off, cd_size, n_entries, i, pos;
   uint32_t best_size = 0, best_comp = 0, best_crc = 0, best_lho = 0, best_method = 0;
   int found = 0, best_is_rom = 0, ret = -1;
   char note[700], tmp[700];

   crc_init();
   if (!f)
      return fail(err, errlen, "Can't open the zip file.");

   /* end of central directory: in the last 64 KB + 22 bytes */
   fseek(f, 0, SEEK_END);
   fsize = ftell(f);
   for (search = fsize - 22; search >= 0 && search >= fsize - 22 - 65535; search--)
   {
      fseek(f, search, SEEK_SET);
      if (fread(buf, 1, 22, f) == 22 && rd32(buf) == 0x06054B50)
         break;
   }
   if (search < 0 || search < fsize - 22 - 65535)
   {
      fclose(f);
      return fail(err, errlen, "Not a zip file (or it's damaged).");
   }
   n_entries = rd16(buf + 10);
   cd_size = rd32(buf + 12);
   cd_off = rd32(buf + 16);
   if (cd_off == 0xFFFFFFFF || (long)cd_off + (long)cd_size > fsize || cd_size > 4 * 1024 * 1024)
   {
      fclose(f);
      return fail(err, errlen, "Unsupported zip (zip64 or damaged).");
   }
   cd = malloc(cd_size);
   fseek(f, cd_off, SEEK_SET);
   if (!cd || fread(cd, 1, cd_size, f) != cd_size)
   {
      free(cd);
      fclose(f);
      return fail(err, errlen, "Can't read the zip's file list.");
   }

   /* pick the first .gba/.agb/.bin, else the largest file */
   for (i = 0, pos = 0; i < n_entries && pos + 46 <= cd_size; i++)
   {
      const uint8_t *e = cd + pos;
      uint32_t nlen, xlen, clen, usize;
      char fname[256];
      int rom;
      if (rd32(e) != 0x02014B50)
         break;
      nlen = rd16(e + 28);
      xlen = rd16(e + 30);
      clen = rd16(e + 32);
      usize = rd32(e + 24);
      if (pos + 46 + nlen > cd_size)
         break;
      snprintf(fname, sizeof(fname), "%.*s", (int)(nlen < 255 ? nlen : 255), (const char*)e + 46);
      rom = is_rom_name(fname);
      if (fname[0] && fname[strlen(fname) - 1] != '/' &&
          (!found || (rom && !best_is_rom) || (rom == best_is_rom && !best_is_rom && usize > best_size)))
      {
         found = 1;
         best_is_rom = rom;
         best_method = rd16(e + 10);
         best_crc = rd32(e + 16);
         best_comp = rd32(e + 20);
         best_size = usize;
         best_lho = rd32(e + 42);
      }
      pos += 46 + nlen + xlen + clen;
   }
   free(cd);
   if (!found)
   {
      fclose(f);
      return fail(err, errlen, "The zip has no game in it.");
   }
   if (best_method != 0 && best_method != 8)
   {
      fclose(f);
      return fail(err, errlen, "The zip uses an unsupported compression\n(only Deflate and Store work).");
   }
   if (best_size > 32 * 1024 * 1024 || best_size == 0)
   {
      fclose(f);
      return fail(err, errlen, "The game in the zip has a wrong size.");
   }

   /* the cache: reuse it if it is this game, unchanged */
   mkdir(cache_dir, 0777);
   snprintf(out_path, out_len, "%s/%s.gba", cache_dir, name);
   snprintf(note, sizeof(note), "%s/%s.crc", cache_dir, name);
   {
      FILE *n = fopen(note, "r");
      unsigned long c = 0, sz = 0;
      struct stat st;
      int ok = n && fscanf(n, "%lx %lu", &c, &sz) == 2 && c == best_crc && sz == best_size &&
               !stat(out_path, &st) && (uint32_t)st.st_size == best_size;
      if (n)
         fclose(n);
      if (ok)
      {
         fclose(f);
         return 0;
      }
   }
   cache_prune(cache_dir, "");   /* make room: only one game is kept */

   /* local header: the data follows its name and extra field */
   fseek(f, best_lho, SEEK_SET);
   if (fread(buf, 1, 30, f) != 30 || rd32(buf) != 0x04034B50)
   {
      fclose(f);
      return fail(err, errlen, "The zip is damaged (local header).");
   }
   fseek(f, best_lho + 30 + rd16(buf + 26) + rd16(buf + 28), SEEK_SET);

   snprintf(tmp, sizeof(tmp), "%s.part", out_path);
   o = fopen(tmp, "wb");
   if (!o)
   {
      fclose(f);
      return fail(err, errlen, "Can't write to the SD card.");
   }
   if (progress)
      progress(best_size);
   {
      static uint8_t obuf[64 * 1024];
      setvbuf(o, (char*)obuf, _IOFBF, sizeof(obuf));
   }
   if (best_method == 0)
   {
      static uint8_t cbuf[32 * 1024];
      uint32_t left = best_size, crc = 0;
      ret = 0;
      if (best_comp != best_size)
         ret = -1;
      while (!ret && left)
      {
         uint32_t n = left < sizeof(cbuf) ? left : sizeof(cbuf);
         if (fread(cbuf, 1, n, f) != n || fwrite(cbuf, 1, n, o) != n)
            ret = -1;
         crc = crc_update(crc, cbuf, n);
         left -= n;
      }
      if (!ret && crc != best_crc)
         ret = -1;
   }
   else
   {
      uint32_t crc = 0;
      ret = inflate_stream(f, best_comp, o, best_size, &crc);
      if (!ret && crc != best_crc)
         ret = -1;
   }
   fclose(f);
   if (fclose(o))
      ret = -1;
   if (ret)
   {
      remove(tmp);
      return fail(err, errlen, "Unzipping failed: the zip is damaged,\nor the SD card is full.");
   }
   remove(out_path);
   if (rename(tmp, out_path))
   {
      remove(tmp);
      return fail(err, errlen, "Can't write to the SD card.");
   }
   {
      FILE *n = fopen(note, "w");
      if (n)
      {
         fprintf(n, "%08lx %lu\n", (unsigned long)best_crc, (unsigned long)best_size);
         fclose(n);
      }
   }
   return 0;
}
