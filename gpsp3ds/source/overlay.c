/* PNG loading for the screen overlay (stb_image, PNG only). */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"
#include "overlay.h"

unsigned char *overlay_png_load(const char *path, int *w, int *h)
{
   int n;
   return stbi_load(path, w, h, &n, 4);
}

void overlay_png_free(unsigned char *p)
{
   stbi_image_free(p);
}
