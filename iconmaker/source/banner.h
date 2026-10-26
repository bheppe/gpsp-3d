#ifndef BANNER_H
#define BANNER_H
#include <stdint.h>

/* Put a 256x128 RGBA picture into a bannertool banner (CBMD), in place.
 * Returns 0, or -1 with a message in err (the banner is then unchanged). */
int banner_set_image(uint8_t *cbmd, uint32_t size, const uint8_t *rgba256x128,
                     char *err, int errlen);

#endif
