#ifndef PNGSHRINK_H
#define PNGSHRINK_H
#include <stdio.h>
#include <stdint.h>

/* A PNG read a row at a time with libpng and shrunk while it's read (each
 * block of f x f pixels averaged, f as big as still gives at least
 * min_w x min_h), as RGBA: a cover of any size needs only a row of it in
 * memory. f is at the PNG's signature. Returns the picture (*w x *h, free()
 * it), or NULL: then *w x *h is the PNG's own size if its header could be
 * read, and *interlaced says if that's why (an interlaced PNG comes in
 * passes, not row by row). *mem is the most memory it needed, in bytes. */
uint8_t *png_shrink(FILE *f, int min_w, int min_h, int max_side,
                    int *w, int *h, int *interlaced, uint32_t *mem);

#endif
