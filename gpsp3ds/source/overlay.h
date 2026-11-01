#pragma once
/* RGBA, 4 bytes per pixel, top row first; NULL if missing or not a PNG */
unsigned char *overlay_png_load(const char *path, int *w, int *h);
void           overlay_png_free(unsigned char *p);
