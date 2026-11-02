/* JPEGs decoded small while they're read: see jpegshrink.h. Plain C and
 * libjpeg (the classic API of libjpeg-turbo), so it also builds on a PC
 * (test/jpegtest.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>
#include "jpegshrink.h"

int (*jpeg_shrink_stop)(void);
uint64_t (*jpeg_shrink_clock)(void);
uint64_t jpeg_shrink_read_ticks;

static uint64_t clock_now(void)
{
   return jpeg_shrink_clock ? jpeg_shrink_clock() : 0;
}

/* errors (and a stop) longjmp back into jpeg_shrink(); warnings (a damaged
 * end, say) are quiet: the picture still comes out */
typedef struct { struct jpeg_error_mgr pub; jmp_buf jb; int stopped; } err_mgr;

static void on_error(j_common_ptr c)
{
   longjmp(((err_mgr*)c->err)->jb, 1);
}

static void on_message(j_common_ptr c)
{
   (void)c;
}

/* called by libjpeg as it goes (rows of input while a progressive JPEG's
 * passes are read, then each row of output); the stop check every 8th */
static void on_progress(j_common_ptr c)
{
   static unsigned calls;
   if (jpeg_shrink_stop && !(++calls & 7) && jpeg_shrink_stop())
   {
      ((err_mgr*)c->err)->stopped = 1;
      longjmp(((err_mgr*)c->err)->jb, 1);
   }
}

/* the file in 64 KB pieces (the SD card is much faster in big reads) */
typedef struct { struct jpeg_source_mgr pub; FILE *f; JOCTET buf[64 * 1024]; } src_mgr;

static void src_init(j_decompress_ptr c)
{
   (void)c;
}

static boolean src_fill(j_decompress_ptr c)
{
   src_mgr *s = (src_mgr*)c->src;
   uint64_t t = clock_now();
   size_t n = fread(s->buf, 1, sizeof(s->buf), s->f);
   jpeg_shrink_read_ticks += clock_now() - t;
   if (!n)
   {
      /* the file ends early: an end marker, as libjpeg's own source does */
      s->buf[0] = 0xFF;
      s->buf[1] = JPEG_EOI;
      n = 2;
   }
   s->pub.next_input_byte = s->buf;
   s->pub.bytes_in_buffer = n;
   return TRUE;
}

static void src_skip(j_decompress_ptr c, long n)
{
   src_mgr *s = (src_mgr*)c->src;
   if (n <= 0)
      return;
   while (n > (long)s->pub.bytes_in_buffer)
   {
      n -= (long)s->pub.bytes_in_buffer;
      src_fill(c);
   }
   s->pub.next_input_byte += n;
   s->pub.bytes_in_buffer -= n;
}

static void src_term(j_decompress_ptr c)
{
   (void)c;
}

/* here, not on the stack, so they're still right after a longjmp */
static struct jpeg_decompress_struct cinfo;
static err_mgr err;
static src_mgr src;
static struct jpeg_progress_mgr progress;
static uint8_t *out;

uint8_t *jpeg_shrink(FILE *f, int min_w, int min_h, int max_side, uint32_t max_coef,
                     int *w, int *h, int *why, uint32_t *mem)
{
   uint32_t jw, jh, ow, oh, n, coef = 0;
   uint8_t *img;
   int ci;

   *w = *h = *why = 0;
   *mem = 0;
   jpeg_shrink_read_ticks = 0;
   out = NULL;
   cinfo.err = jpeg_std_error(&err.pub);
   err.pub.error_exit = on_error;
   err.pub.output_message = on_message;
   err.stopped = 0;
   if (setjmp(err.jb))
   {
      if (err.stopped)
         *why = JPEG_SHRINK_STOPPED;
      free(out);
      out = NULL;
      jpeg_destroy_decompress(&cinfo);
      return NULL;
   }
   jpeg_create_decompress(&cinfo);
   src.pub.init_source = src_init;
   src.pub.fill_input_buffer = src_fill;
   src.pub.skip_input_data = src_skip;
   src.pub.resync_to_restart = jpeg_resync_to_restart;
   src.pub.term_source = src_term;
   src.pub.bytes_in_buffer = 0;
   src.pub.next_input_byte = NULL;
   src.f = f;
   cinfo.src = &src.pub;
   progress.progress_monitor = on_progress;
   cinfo.progress = &progress;

   jpeg_read_header(&cinfo, TRUE);
   jw = cinfo.image_width;
   jh = cinfo.image_height;
   *w = (int)jw;
   *h = (int)jh;
   if (jw > (uint32_t)max_side || jh > (uint32_t)max_side)
   {
      *why = JPEG_SHRINK_TOO_BIG;
      jpeg_destroy_decompress(&cinfo);
      return NULL;
   }
   if (jpeg_has_multiple_scans(&cinfo))
   {
      /* 64 coefficients of 2 bytes in each 8x8 block of each channel */
      uint64_t blocks = 0;
      for (ci = 0; ci < cinfo.num_components; ci++)
         blocks += (uint64_t)cinfo.comp_info[ci].width_in_blocks * cinfo.comp_info[ci].height_in_blocks;
      if (blocks * 64 * sizeof(JCOEF) > max_coef)
      {
         *why = JPEG_SHRINK_TOO_BIG;
         jpeg_destroy_decompress(&cinfo);
         return NULL;
      }
      coef = (uint32_t)(blocks * 64 * sizeof(JCOEF));
   }
   /* the smallest n/8 that still gives min_w x min_h (8/8 if it's smaller) */
   for (n = 1; n < 8; n++)
      if ((jw * n + 7) / 8 >= (uint32_t)min_w && (jh * n + 7) / 8 >= (uint32_t)min_h)
         break;
   cinfo.scale_num = n;
   cinfo.scale_denom = 8;
   cinfo.out_color_space = JCS_EXT_RGBA;

   jpeg_start_decompress(&cinfo);
   ow = cinfo.output_width;
   oh = cinfo.output_height;
   if (!(out = malloc((size_t)ow * oh * 4)))
      longjmp(err.jb, 1);
   while (cinfo.output_scanline < oh)
   {
      JSAMPROW r = out + (size_t)cinfo.output_scanline * ow * 4;
      jpeg_read_scanlines(&cinfo, &r, 1);
   }
   /* no jpeg_finish_decompress(): what comes after the picture doesn't matter */
   img = out;
   out = NULL;
   jpeg_destroy_decompress(&cinfo);
   *w = (int)ow;
   *h = (int)oh;
   *mem = (uint32_t)((size_t)ow * oh * 4 + coef + sizeof(src.buf));
   return img;
}
