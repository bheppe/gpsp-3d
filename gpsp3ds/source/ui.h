/* Small software UI kit for the bottom screen: anti-aliased text (Inter),
 * rounded panels, circles and arrows, drawn into a 320x240 RGB565 canvas
 * that is then copied to the (rotated) bottom framebuffer. */
#ifndef UI_KIT_H
#define UI_KIT_H

#include <stdint.h>
#include <stdbool.h>

#define UI_W 320
#define UI_H 240

typedef struct { int8_t x, y; uint8_t w, h, adv; uint32_t off; } ui_glyph;
typedef struct { int height, ascent; const ui_glyph *g; const unsigned char *bits; } ui_font;

extern const ui_font ui_font_small, ui_font_body, ui_font_bold, ui_font_title, ui_font_big;

#define UI_RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

extern uint16_t ui_canvas[UI_W * UI_H];

void ui_clear(uint16_t c);
void ui_fill(int x, int y, int w, int h, uint16_t c);
void ui_fill_alpha(int x, int y, int w, int h, uint16_t c, int alpha /* 0..256 */);
void ui_vgradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom);
void ui_rrect(int x, int y, int w, int h, int r, uint16_t c);
void ui_rrect_outline(int x, int y, int w, int h, int r, int t, uint16_t c);
void ui_circle(int cx, int cy, int r, uint16_t c);
void ui_arrow(int cx, int cy, int size, int dir /* -1 left, 1 right, 2 up, -2 down */, uint16_t c);

/* text drawn only on rows y0 <= y < y1 (ui_set_clip_y(0, UI_H) for all) */
void ui_set_clip_y(int y0, int y1);

int  ui_text_w(const ui_font *f, const char *s);
/* draws s with its top-left at (x, y); clips to max_w pixels with "..." if
 * max_w > 0. Returns the x after the text. */
int  ui_text(const ui_font *f, int x, int y, const char *s, uint16_t c, int max_w);
void ui_text_center(const ui_font *f, int cx, int y, const char *s, uint16_t c);
void ui_text_right(const ui_font *f, int rx, int y, const char *s, uint16_t c);
/* vertically centred in a box of height h starting at y */
int  ui_text_vc(const ui_font *f, int x, int y, int h, const char *s, uint16_t c, int max_w);

/* copy the canvas to the bottom screen and show it (3DS only) */
void ui_present(void);

#endif
