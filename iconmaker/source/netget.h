#ifndef NETGET_H
#define NETGET_H
#include <stddef.h>
#include <stdint.h>

/* Files over HTTPS into memory with libcurl (devkitPro's 3ds-curl: the
 * 3DS's own HTTPS is too old for most servers). Plain C, so it also builds
 * on a PC. On the 3DS, socInit() before net_begin(), socExit() after
 * net_end().
 *
 * net_begin() .. net_end() keep one connection open for several files.
 * net_get() returns the HTTP status (200, 404, ...) with the file in *data
 * (free() it; NUL-terminated, not counted in *size), or -1: no answer, too
 * big (over max bytes), out of memory or stopped (err says which). */
void net_begin(void);
void net_end(void);
int net_get(const char *url, size_t max, uint8_t **data, size_t *size, char *err, int errlen);

/* If set, called while a file comes in: when net_stop returns nonzero,
 * net_get() stops (-1, err "stopped"); net_progress gets the bytes so far
 * and the size (0 if the server didn't say). */
extern int (*net_stop)(void);
extern void (*net_progress)(size_t got, size_t total);

#endif
