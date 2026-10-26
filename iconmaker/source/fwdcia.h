/* Makes a HOME Menu icon (a forwarder CIA) for one game from a template
 * forwarder CIA, on the 3DS itself.
 *
 * The template is built once on a PC by make-template.sh: the gpSP 3D
 * forwarder program, unencrypted, with the title ID 0x000400000F3D0000.
 * fwdcia_make() then fills in, in place:
 *   - a new title ID (everywhere the template's appears),
 *   - the forwarder's slot: the game's path, the emulator to start and the
 *     file that emulator reads the game's path from,
 *   - the HOME Menu name and icon (SMDH) and the top screen banner,
 * and recomputes every hash the system checks. Signatures are left as they
 * are (custom firmware with signature patches, like Luma3DS, accepts them,
 * as for any homebrew CIA).
 */
#ifndef FWDCIA_H
#define FWDCIA_H

#include <stdint.h>

#define FWDCIA_TEMPLATE_TID 0x000400000F3D0000ULL
#define FWDCIA_SMDH_SIZE    0x36C0

/* SHA-256 of size bytes at data into out (on the 3DS: FSUSER_UpdateSha256Context) */
typedef void (*fwdcia_sha256_fn)(const void *data, uint32_t size, uint8_t out[32]);

typedef struct
{
   uint64_t    title_id;   /* the new forwarder's title ID (0x00040000xxxxxx00) */
   uint64_t    target;     /* title ID of the emulator to start (0: gpSP 3D) */
   const char *launch;     /* file that emulator reads the game path from ("" : gpSP 3D's) */
   const char *path;       /* the game, e.g. "sdmc:/roms/gba/Golf.gba" */
   const uint8_t *smdh;    /* FWDCIA_SMDH_SIZE bytes, or NULL to keep the template's */
   const uint8_t *banner;  /* 256x128 RGBA top-screen picture, or NULL to keep the template's */
} fwdcia_params;

/* Returns 0, or -1 with a message in err. */
int fwdcia_make(uint8_t *cia, uint32_t size, const fwdcia_params *p,
                fwdcia_sha256_fn sha256, char *err, int errlen);

/* A title ID for a game, from its path: 0x00040000 000C0000-000EFF00 (away
 * from gpSP 3D's own and the template's). Same as make-forwarders.sh's
 * range, though not the same numbers. */
uint64_t fwdcia_title_id_for(const char *path);

/* --- SMDH (HOME Menu name and icon) --- */

/* Fill in a SMDH (start from the template's, so the rest stays valid):
 * the name in every language, and the icon from a 48x48 RGBA picture
 * (rgba48 may be NULL to keep the icon). Transparent pixels show white. */
void smdh_set(uint8_t *smdh, const char *short_title, const char *long_title,
              const char *publisher, const uint8_t *rgba48);

/* Scale a w x h RGBA picture to 48x48 (area average), keeping its aspect
 * ratio and centering it on transparent. */
void icon_scale48(const uint8_t *rgba, int w, int h, uint8_t *out48);

/* Scale a w x h RGBA picture to fit ow x oh (area average when shrinking),
 * keeping its aspect ratio and centering it on transparent. */
void image_fit(const uint8_t *rgba, int w, int h, uint8_t *out, int ow, int oh);

#endif
