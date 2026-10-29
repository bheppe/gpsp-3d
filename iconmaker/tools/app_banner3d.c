/* Makes an app's 3D HOME Menu banner (a CBMD file) on the PC, from the 3D
 * model Icon Maker uses (res/banner3d.cgfx): the same tile that comes out of
 * the screen, with the app's own pictures and sound.
 *
 *   gcc -O2 -Isource tools/app_banner3d.c -lm -o app_banner3d
 *   ./app_banner3d res/banner3d.cgfx back.png label.png sound.bcwav out.bnr
 *
 * back.png   256x128, the backdrop (the 3D view shows x 21..235 of it)
 * label.png  128x128, the face of the tile (the app's icon)
 * sound      a CWAV (the sound part of a banner made by bannertool)
 */
#include "../source/banner.c"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../source/stb_image.h"

static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static uint8_t *load(const char *path, long *size)
{
   FILE *f = fopen(path, "rb");
   uint8_t *b;
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   *size = ftell(f);
   rewind(f);
   b = malloc(*size);
   if (fread(b, 1, *size, f) != (size_t)*size)
      b = NULL;
   fclose(f);
   return b;
}

int main(int argc, char **argv)
{
   long glen, wlen;
   int bw, bh, lw, lh, n;
   uint8_t *g, *wav, *back, *label, *packed, *out;
   uint32_t t_back, t_label, plen, cwav_off, total;
   FILE *f;

   if (argc != 6)
   {
      fprintf(stderr, "usage: app_banner3d model.cgfx back.png label.png sound.bcwav out.bnr\n");
      return 2;
   }
   g = load(argv[1], &glen);
   wav = load(argv[4], &wlen);
   back = stbi_load(argv[2], &bw, &bh, &n, 4);
   label = stbi_load(argv[3], &lw, &lh, &n, 4);
   if (!g || !wav || !back || !label || bw != 256 || bh != 128 || lw != 128 || lh != 128 ||
       memcmp(g, "CGFX", 4) || memcmp(wav, "CWAV", 4))
   {
      fprintf(stderr, "app_banner3d: bad input (sizes: back 256x128, label 128x128)\n");
      return 1;
   }
   t_back = find_txob(g, glen, 256, 128);
   t_label = find_txob(g, glen, 128, 128);
   if (!t_back || !t_label || put_texture(g, glen, t_back, 256, 128, back, 1) ||
       put_texture(g, glen, t_label, 128, 128, label, 1))
   {
      fprintf(stderr, "app_banner3d: not the 3D model\n");
      return 1;
   }
   packed = malloc(glen * 2);
   plen = lz11_compress(g, glen, packed, glen * 2);
   if (!plen)
      return 1;
   cwav_off = (0x88 + plen + 0x1F) & ~0x1F;
   total = cwav_off + wlen;
   out = calloc(1, total);
   memcpy(out, "CBMD", 4);
   wr32(out + 0x08, 0x88);           /* the common model; no per-region ones */
   wr32(out + 0x84, cwav_off);
   memcpy(out + 0x88, packed, plen);
   memcpy(out + cwav_off, wav, wlen);
   f = fopen(argv[5], "wb");
   if (!f || fwrite(out, 1, total, f) != total)
      return 1;
   fclose(f);
   printf("wrote %s (%u bytes)\n", argv[5], total);
   return 0;
}
