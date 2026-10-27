#ifndef GPSP3D_UNZIP_H
#define GPSP3D_UNZIP_H
#include <stdint.h>

/* called once before a game is unpacked (not when the cache is reused),
 * with its size, so the app can show a message */
typedef void (*unzip_progress_fn)(uint32_t size);

/* Unpack the game in zip_path to cache_dir/<name>.gba (reusing it if it is
 * already there and unchanged) and put that path in out_path. Returns 0, or
 * -1 with a message in err. */
int unzip_rom(const char *zip_path, const char *cache_dir, const char *name,
              char *out_path, int out_len, unzip_progress_fn progress,
              char *err, int errlen);

#endif
