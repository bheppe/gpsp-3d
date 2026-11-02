#ifndef JPEGSHRINK_H
#define JPEGSHRINK_H
#include <stdio.h>
#include <stdint.h>

/* A JPEG read from f in small pieces with libjpeg and decoded straight at
 * the smallest size it can (1/8, 2/8, ... of its own) that is still at
 * least min_w x min_h, as RGBA: the file is never in memory whole. f is at
 * the JPEG's start. Returns the picture (*w x *h, free() it), or NULL: then
 * *w x *h is the JPEG's own size if its header could be read, and *why one
 * of the reasons below (0: it couldn't be read, e.g. a CMYK JPEG). *mem is
 * about the most memory it needed, in bytes.
 *
 * A progressive JPEG (in several passes) needs all of its coefficients in
 * memory before any of it comes out, about 2 bytes a pixel and channel at
 * its own size whatever the output size: over max_coef bytes, it's
 * refused as too big. So is a side over max_side. */
enum { JPEG_SHRINK_TOO_BIG = 1, JPEG_SHRINK_STOPPED = 2 };
uint8_t *jpeg_shrink(FILE *f, int min_w, int min_h, int max_side, uint32_t max_coef,
                     int *w, int *h, int *why, uint32_t *mem);

/* If set, called as the JPEG is read and decoded (every few rows): when it
 * returns nonzero, jpeg_shrink() stops (*why JPEG_SHRINK_STOPPED). */
extern int (*jpeg_shrink_stop)(void);

/* Timing (Icon Maker's TIMING build): set jpeg_shrink_clock to a clock and
 * the last jpeg_shrink() adds up, in its ticks, the time spent reading the
 * file. */
extern uint64_t (*jpeg_shrink_clock)(void);
extern uint64_t jpeg_shrink_read_ticks;

#endif
