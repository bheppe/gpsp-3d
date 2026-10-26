#ifndef DEFICON_H
#define DEFICON_H
#include <stdint.h>

/* A 48x48 RGBA icon for a game with no PNG: a cartridge with its name on
 * the label ("(USA)" and the like left out), colored by the name. */
void default_icon(const char *name, uint8_t *rgba48);

/* A 256x128 RGBA banner for a game with no banner PNG: its icon (iw x ih
 * RGBA, shown 96x96 at (12, 16)) and, with_text, its name in a pixel font on
 * gpSP 3D's navy background. (The app writes the name itself, in Inter.) */
void default_banner(const char *name, const uint8_t *icon, int iw, int ih,
                    uint8_t *rgba256x128, int with_text);

/* the name without "(USA)", "[!]" and the like, and with '_' as spaces */
void clean_name(const char *in, char *out, int n);

/* A 256x128 banner from any picture: fitted, centered on the navy. */
void banner_from_image(const uint8_t *img, int w, int h, uint8_t *rgba256x128);

#endif
