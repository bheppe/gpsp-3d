/* PC test for jpegshrink: decodes a JPEG as Icon Maker does for an icon
 * (at least 128x128) and writes the result as raw RGBA:
 *   jpegtest picture.jpg out.rgba [min_w min_h]
 * prints "WxH mem_bytes", or why it couldn't. */
#include <stdio.h>
#include <stdlib.h>
#include "../source/jpegshrink.h"

int main(int argc, char **argv)
{
   int w, h, why, min_w = 128, min_h = 128;
   uint32_t mem;
   uint8_t *img;
   FILE *f;
   if (argc < 3) { fprintf(stderr, "usage: jpegtest picture.jpg out.rgba [min_w min_h]\n"); return 2; }
   if (argc > 4) { min_w = atoi(argv[3]); min_h = atoi(argv[4]); }
   if (!(f = fopen(argv[1], "rb"))) { perror(argv[1]); return 1; }
   img = jpeg_shrink(f, min_w, min_h, 4096, 16 << 20, &w, &h, &why, &mem);
   fclose(f);
   if (!img) { printf("no picture: %dx%d%s\n", w, h, why == JPEG_SHRINK_TOO_BIG ? " too big" : ""); return 1; }
   f = fopen(argv[2], "wb");
   fwrite(img, 4, (size_t)w * h, f);
   fclose(f);
   printf("%dx%d %lu\n", w, h, (unsigned long)mem);
   free(img);
   return 0;
}
