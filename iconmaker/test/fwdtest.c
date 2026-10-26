/* PC test for fwdcia, doing what the app does:
 *   fwdtest template.cia out.cia title_id_hex "game path" [name [icon.png|- [banner.png|-]]]
 * With a name: the icon from icon.png, else the cartridge; the banner from
 * banner.png, else the icon bigger with the name. Without: only the
 * title ID and the path change. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../source/fwdcia.h"
#include "../source/deficon.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../source/stb_image.h"
void sha256(const void *data, uint32_t len, uint8_t out[32]);

int main(int argc, char **argv)
{
   FILE *f; long n; uint8_t *cia; char err[256]; fwdcia_params p;
   static uint8_t smdh[FWDCIA_SMDH_SIZE], icon[48 * 48 * 4], banner[256 * 128 * 4];
   if (argc < 5) { fprintf(stderr, "usage\n"); return 2; }
   f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
   cia = malloc(n); if (fread(cia, 1, n, f) != (size_t)n) return 1; fclose(f);
   memset(&p, 0, sizeof(p));
   p.title_id = strtoull(argv[3], NULL, 16); p.path = argv[4]; p.launch = "";
   if (argc > 5) {
      long i; int iw = 48, ih = 48, bw, bh, c; uint8_t *ipng = NULL, *bpng = NULL;
      for (i = 0; i + FWDCIA_SMDH_SIZE <= n; i++) if (!memcmp(cia + i, "SMDH", 4)) break;
      memcpy(smdh, cia + i, FWDCIA_SMDH_SIZE);
      if (argc > 6 && strcmp(argv[6], "-")) ipng = stbi_load(argv[6], &iw, &ih, &c, 4);
      if (ipng) icon_scale48(ipng, iw, ih, icon); else { default_icon(argv[5], icon); iw = ih = 48; }
      if (argc > 7 && strcmp(argv[7], "-")) bpng = stbi_load(argv[7], &bw, &bh, &c, 4);
      if (bpng) banner_from_image(bpng, bw, bh, banner);
      else default_banner(argv[5], ipng ? ipng : icon, iw, ih, banner, 1);
      smdh_set(smdh, argv[5], argv[5], "gpSP 3D", icon);
      p.smdh = smdh; p.banner = banner;
      f = fopen("banner_in.rgba", "wb"); fwrite(banner, 1, sizeof(banner), f); fclose(f);
   }
   if (fwdcia_make(cia, n, &p, sha256, err, sizeof(err))) { fprintf(stderr, "fwdcia: %s\n", err); return 1; }
   f = fopen(argv[2], "wb"); fwrite(cia, 1, n, f); fclose(f);
   printf("ok %s\n", argv[2]);
   return 0;
}
