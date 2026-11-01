/* PNGs shrunk while they're read: see pngshrink.h. Plain C and libpng, so
 * it also builds on a PC (test/pngtest.c). */
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include "pngshrink.h"

int (*png_shrink_stop)(void);
uint64_t (*png_shrink_clock)(void);
uint64_t png_shrink_read_ticks, png_shrink_rows_ticks, png_shrink_average_ticks;

static uint64_t clock_now(void)
{
   return png_shrink_clock ? png_shrink_clock() : 0;
}

/* libpng's reads, from the FILE (and timed) */
static void read_data(png_structp png, png_bytep data, size_t len)
{
   uint64_t t = clock_now();
   if (fread(data, 1, len, (FILE*)png_get_io_ptr(png)) != len)
      png_error(png, "read error");
   png_shrink_read_ticks += clock_now() - t;
}

/* the buffers, here so an error (a longjmp out of libpng) can free them */
static uint8_t *row, *out;
static uint32_t *acc;

static void free_buffers(void)
{
   free(row);
   free(out);
   free(acc);
   row = out = NULL;
   acc = NULL;
}

uint8_t *png_shrink(FILE *f, int min_w, int min_h, int max_side,
                    int *w, int *h, int *interlaced, uint32_t *mem)
{
   png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
   png_infop info = png ? png_create_info_struct(png) : NULL;
   uint32_t pw, ph, ow, oh, fac, x, y, k;
   uint8_t *img;

   *w = *h = *interlaced = 0;
   *mem = 0;
   png_shrink_read_ticks = png_shrink_rows_ticks = png_shrink_average_ticks = 0;
   if (!info)
   {
      png_destroy_read_struct(&png, NULL, NULL);
      return NULL;
   }
   if (setjmp(png_jmpbuf(png)))
   {
      free_buffers();
      png_destroy_read_struct(&png, &info, NULL);
      return NULL;
   }
   png_set_read_fn(png, f, read_data);
   /* no checksums, as stb_image: they cost time and only tell a damaged
    * file, which then shows damaged instead of not at all */
   png_set_crc_action(png, PNG_CRC_QUIET_USE, PNG_CRC_QUIET_USE);
#ifdef PNG_IGNORE_ADLER32
   png_set_option(png, PNG_IGNORE_ADLER32, PNG_OPTION_ON);
#endif
   png_read_info(png, info);
   pw = png_get_image_width(png, info);
   ph = png_get_image_height(png, info);
   *w = (int)pw;
   *h = (int)ph;
   if (png_get_interlace_type(png, info) != PNG_INTERLACE_NONE || pw > (uint32_t)max_side ||
       ph > (uint32_t)max_side || !pw || !ph)
   {
      *interlaced = png_get_interlace_type(png, info) != PNG_INTERLACE_NONE;
      png_destroy_read_struct(&png, &info, NULL);
      return NULL;
   }
   /* any PNG to 8-bit RGBA */
   png_set_expand(png);
   png_set_strip_16(png);
   png_set_gray_to_rgb(png);
   png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
   png_read_update_info(png, info);

   fac = pw / (uint32_t)min_w < ph / (uint32_t)min_h ? pw / (uint32_t)min_w : ph / (uint32_t)min_h;
   if (fac < 1)
      fac = 1;
   ow = (pw + fac - 1) / fac;
   oh = (ph + fac - 1) / fac;
   row = malloc(png_get_rowbytes(png, info));
   acc = calloc(ow * 5, sizeof(uint32_t));
   out = malloc((size_t)ow * oh * 4);
   if (!row || !acc || !out)
      png_error(png, "out of memory");
   *mem = (uint32_t)(png_get_rowbytes(png, info) + ow * 5 * sizeof(uint32_t) + (size_t)ow * oh * 4);

   for (y = 0; y < ph; y++)
   {
      const uint8_t *p = row;
      uint32_t *a = acc;
      uint64_t t;
      if (png_shrink_stop && !(y & 15) && png_shrink_stop())
         png_error(png, "stopped");
      t = clock_now();
      png_read_row(png, row, NULL);
      png_shrink_rows_ticks += clock_now() - t;
      t = clock_now();
      /* each block of fac pixels summed in registers, then added to its
       * column once; colour weighted by alpha, so see-through edges don't
       * darken (and no x / fac: the 3DS's CPU has no divide) */
      for (x = 0; x < pw; x += k, a += 5)
      {
         uint32_t r = 0, g = 0, b = 0, al = 0, i;
         k = pw - x < fac ? pw - x : fac;
         for (i = 0; i < k; i++, p += 4)
         {
            uint32_t pa = p[3];
            r += p[0] * pa;
            g += p[1] * pa;
            b += p[2] * pa;
            al += pa;
         }
         a[0] += r;
         a[1] += g;
         a[2] += b;
         a[3] += al;
         a[4] += k;
      }
      if ((y + 1) % fac == 0 || y + 1 == ph)
      {
         uint8_t *o = out + (size_t)(y / fac) * ow * 4;
         for (x = 0; x < ow; x++, o += 4)
         {
            const uint32_t *a = acc + x * 5;
            o[0] = (uint8_t)(a[3] ? a[0] / a[3] : 0);
            o[1] = (uint8_t)(a[3] ? a[1] / a[3] : 0);
            o[2] = (uint8_t)(a[3] ? a[2] / a[3] : 0);
            o[3] = (uint8_t)(a[3] / a[4]);
         }
         memset(acc, 0, ow * 5 * sizeof(uint32_t));
      }
      png_shrink_average_ticks += clock_now() - t;
   }
   img = out;
   out = NULL;
   free_buffers();
   png_destroy_read_struct(&png, &info, NULL);
   *w = (int)ow;
   *h = (int)oh;
   return img;
}
