/* Guide viewer: a plain-text guide (a walkthrough, a FAQ) for the game
 * shown on the bottom screen while it runs. */
#ifndef GPSP3D_GUIDE_H
#define GPSP3D_GUIDE_H
#include <stdbool.h>

/* Look for the game's guide: <folder of the game>/<name>.txt, then
 * <app_dir>/guides/<name>.txt. Loads it and where it was left (and whether
 * it was showing). Returns whether there is one. */
bool guide_open(const char *rom_path, const char *rom_name, const char *app_dir);
/* remembers the place and frees the guide */
void guide_close(void);
void guide_save_pos(void);

bool guide_available(void);
const char *guide_file(void);   /* path of the guide that was found */
const char *guide_error(void);  /* why it could not be loaded, or NULL */
extern bool guide_shown;        /* on the bottom screen instead of the stats */

/* draw it into the UI canvas (the caller presents it) */
void guide_draw(void);

/* touch on the bottom screen while the guide is shown: down (first frame),
 * held, or released. Returns GUIDE_REDRAW, GUIDE_MENU or 0. */
enum { GUIDE_TOUCH_DOWN, GUIDE_TOUCH_HELD, GUIDE_TOUCH_UP };
enum { GUIDE_REDRAW = 1, GUIDE_MENU = 2 };
int guide_touch(int phase, int x, int y);

#endif
