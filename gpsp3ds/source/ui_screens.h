/* Bottom-screen layouts: settings menu, game list, in-game panel, messages.
 * Pure drawing + hit testing on ui.h; the app fills in the data. */
#ifndef UI_SCREENS_H
#define UI_SCREENS_H

#include "ui.h"

#define GPSP3D_VERSION "0.1.5"

enum { ROW_HEADER, ROW_ACTION, ROW_CHOICE, ROW_TOGGLE, ROW_SLIDER, ROW_INFO };

typedef struct {
   int kind;
   const char *label;
   const char *value;      /* CHOICE / INFO text, SLIDER value text */
   int on;                 /* TOGGLE */
   int vmin, vmax, v;      /* SLIDER */
   int danger;             /* ACTION drawn in red */
   int disabled;           /* greyed out */
} ui_row;

/* ---- settings menu ---- */
#define MENU_TAB_H     30
#define MENU_LIST_Y    34
#define MENU_ROW_H     28
#define MENU_ROWS_VIS  6
#define MENU_FOOT_Y    (MENU_LIST_Y + MENU_ROWS_VIS * MENU_ROW_H + 4)

typedef struct {
   const char *const *tabs; int ntabs, tab;
   const ui_row *rows; int nrows, sel, scroll;
   const char *help;       /* help for the selected row (\n allowed) */
   const char *note;       /* result of the last action, or NULL */
   int note_ok;
} ui_menu;

void scr_menu(const ui_menu *m);

enum { HIT_NONE, HIT_TAB, HIT_ROW };
enum { ZONE_LABEL, ZONE_DEC, ZONE_INC, ZONE_SLIDER, ZONE_TOGGLE };
typedef struct { int what, tab, row, zone, slider_v; } ui_hit;
ui_hit scr_menu_hit(const ui_menu *m, int x, int y);

/* ---- game list ---- */
#define LIST_Y      40
#define LIST_ROW_H  24
#define LIST_ROWS   7
void scr_browser(const char *path, const char *const *names, const unsigned char *isdir,
                 int n, int sel, int top);
int  scr_browser_hit(int y, int top, int n);    /* index or -1 */

/* ---- in-game panel ---- */
typedef struct {
   const char *title;
   float fps, pictures;    /* emulated frames/s, drawn pictures/s */
   int stereo;             /* 3D shown */
   int depth_pct;          /* 3D strength incl. slider, for the badge */
   int show_stats;
   const char *stat_label[6];
   char stat_value[6][40];
   int nstats;
   const char *warning;    /* e.g. "No sound: dspfirm.cdc missing" */
} ui_hud;
void scr_hud(const ui_hud *h);

/* ---- simple message card ---- */
void scr_message(const char *title, const char *body, const char *hint);

#endif
