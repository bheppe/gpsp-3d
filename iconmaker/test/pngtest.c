/* PC test for pngshrink: shrinks a PNG as Icon Maker does for an icon
 * (at least 128x128) and writes the result as raw RGBA:
 *   pngtest picture.png out.rgba [min_w min_h]
 * prints "WxH mem_bytes", or why it couldn't. */
#include <stdio.h>
#include <stdlib.h>
#include "../source/pngshrink.h"

int main(int argc, char **argv)
{
   int w, h, interlaced, min_w = 128, min_h = 128;
   uint32_t mem;
   uint8_t *img;
   FILE *f;
   if (argc < 3) { fprintf(stderr, "usage: pngtest picture.png out.rgba [min_w min_h]\n"); return 2; }
   if (argc > 4) { min_w = atoi(argv[3]); min_h = atoi(argv[4]); }
   if (!(f = fopen(argv[1], "rb"))) { perror(argv[1]); return 1; }
   img = png_shrink(f, min_w, min_h, 4096, &w, &h, &interlaced, &mem);
   fclose(f);
   if (!img) { printf("no picture: %dx%d%s\n", w, h, interlaced ? " interlaced" : ""); return 1; }
   f = fopen(argv[2], "wb");
   fwrite(img, 4, (size_t)w * h, f);
   fclose(f);
   printf("%dx%d %lu\n", w, h, (unsigned long)mem);
   free(img);
   return 0;
}
