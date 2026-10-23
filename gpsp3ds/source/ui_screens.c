#include "ui_screens.h"
#include <stdio.h>
#include <string.h>

/* theme */
#define C_BG      UI_RGB(0x0E, 0x11, 0x17)
#define C_BG2     UI_RGB(0x16, 0x1A, 0x23)
#define C_PANEL   UI_RGB(0x1B, 0x20, 0x2A)
#define C_ROW     UI_RGB(0x20, 0x26, 0x31)
#define C_ROW_SEL UI_RGB(0x2A, 0x33, 0x45)
#define C_ACCENT  UI_RGB(0x4E, 0x8D, 0xFF)
#define C_ACCENT2 UI_RGB(0x8F, 0x6B, 0xFF)
#define C_TEXT    UI_RGB(0xEC, 0xEF, 0xF4)
#define C_DIM     UI_RGB(0x8B, 0x94, 0xA7)
#define C_FAINT   UI_RGB(0x4A, 0x52, 0x63)
#define C_TRACK   UI_RGB(0x37, 0x3F, 0x50)
#define C_GOOD    UI_RGB(0x5F, 0xD3, 0x93)
#define C_BAD     UI_RGB(0xFF, 0x6E, 0x6E)
#define C_WARN    UI_RGB(0xFF, 0xC1, 0x5C)

#define ROW_X  6
#define ROW_W  (UI_W - 12)
#define VAL_R  (ROW_X + ROW_W - 10)     /* right edge of values */
#define CHOICE_W 120                    /* width of the < value > control */
#define SLIDER_X0 150
#define SLIDER_X1 (UI_W - 64)

static void draw_help(int y, int h, const char *help, const char *note, int note_ok)
{
   ui_fill(0, y, UI_W, h, C_BG2);
   ui_fill(0, y, UI_W, 1, C_PANEL);
   if (note && note[0])
   {
      ui_circle(14, y + 13, 4, note_ok ? C_GOOD : C_BAD);
      ui_text(&ui_font_body, 24, y + 4, note, note_ok ? C_GOOD : C_BAD, UI_W - 32);
      return;
   }
   if (help)
   {
      char line[96];
      const char *nl = strchr(help, '\n');
      int ly = y + 4;
      if (nl)
      {
         int n = (int)(nl - help);
         if (n > 95) n = 95;
         memcpy(line, help, n);
         line[n] = 0;
         ui_text(&ui_font_small, 10, ly, line, C_DIM, UI_W - 20);
         ui_text(&ui_font_small, 10, ly + 15, nl + 1, C_DIM, UI_W - 20);
      }
      else
         ui_text(&ui_font_small, 10, ly + 7, help, C_DIM, UI_W - 20);
   }
}

static void draw_toggle(int rx, int cy, int on, int disabled)
{
   int w = 36, h = 20, x = rx - w, y = cy - h / 2;
   ui_rrect(x, y, w, h, h / 2, on && !disabled ? C_ACCENT : C_TRACK);
   ui_circle(on ? x + w - h / 2 - 1 : x + h / 2, cy, 7, disabled ? C_DIM : C_TEXT);
}

static void draw_choice(int rx, int y, int h, const char *value, int sel, int disabled)
{
   int x0 = rx - CHOICE_W, cy = y + h / 2;
   uint16_t ac = disabled ? C_FAINT : (sel ? C_ACCENT : C_DIM);
   ui_rrect(x0, y + 4, CHOICE_W, h - 8, (h - 8) / 2, sel ? C_ROW : C_BG2);
   ui_arrow(x0 + 12, cy, 4, -1, ac);
   ui_arrow(rx - 12, cy, 4, 1, ac);
   {
      const ui_font *f = &ui_font_bold;
      int tw = ui_text_w(f, value);
      int maxw = CHOICE_W - 40;
      if (tw > maxw) tw = maxw;
      ui_text_vc(f, x0 + (CHOICE_W - tw) / 2, y, h, value, disabled ? C_DIM : C_TEXT, maxw);
   }
}

static void draw_slider(int y, int h, const ui_row *r, int sel)
{
   int cy = y + h / 2, x0 = SLIDER_X0, x1 = SLIDER_X1;
   int span = r->vmax - r->vmin, kx;
   if (span <= 0) span = 1;
   kx = x0 + (r->v - r->vmin) * (x1 - x0) / span;
   ui_rrect(x0, cy - 2, x1 - x0, 4, 2, C_TRACK);
   /* fill from the neutral point (0 if in range, else the minimum) */
   {
      int zero = (r->vmin < 0 && r->vmax > 0) ? x0 + (0 - r->vmin) * (x1 - x0) / span : x0;
      int a = zero < kx ? zero : kx, b = zero < kx ? kx : zero;
      if (b > a)
         ui_rrect(a, cy - 2, b - a, 4, 2, r->disabled ? C_FAINT : C_ACCENT);
   }
   ui_circle(kx, cy, sel ? 8 : 7, r->disabled ? C_DIM : C_TEXT);
   if (sel)
      ui_circle(kx, cy, 3, C_ACCENT);
   ui_text_right(&ui_font_bold, VAL_R, y + (h - ui_font_bold.ascent) / 2 - 1, r->value ? r->value : "",
                 r->disabled ? C_DIM : C_TEXT);
}

void scr_menu(const ui_menu *m)
{
   int i, tw = UI_W / m->ntabs;
   ui_clear(C_BG);

   /* tabs */
   ui_vgradient(0, 0, UI_W, MENU_TAB_H, C_BG2, C_BG);
   for (i = 0; i < m->ntabs; i++)
   {
      int x = i * tw;
      int on = i == m->tab;
      if (on)
      {
         ui_rrect(x + 4, 4, tw - 8, MENU_TAB_H - 8, 10, C_ROW_SEL);
         ui_fill(x + tw / 2 - 12, MENU_TAB_H - 3, 24, 3, C_ACCENT);
      }
      {
         const ui_font *f = on ? &ui_font_bold : &ui_font_body;
         int w = ui_text_w(f, m->tabs[i]);
         ui_text_vc(f, x + (tw - w) / 2, 2, MENU_TAB_H - 4, m->tabs[i], on ? C_TEXT : C_DIM, 0);
      }
   }

   /* rows */
   for (i = 0; i < MENU_ROWS_VIS && m->scroll + i < m->nrows; i++)
   {
      const ui_row *r = &m->rows[m->scroll + i];
      int y = MENU_LIST_Y + i * MENU_ROW_H, h = MENU_ROW_H - 3;
      int sel = m->scroll + i == m->sel;
      if (r->kind == ROW_HEADER)
      {
         ui_text_vc(&ui_font_small, ROW_X + 6, y + 6, h - 6, r->label, C_ACCENT, 0);
         continue;
      }
      ui_rrect(ROW_X, y, ROW_W, h, 8, sel ? C_ROW_SEL : C_PANEL);
      if (sel)
         ui_fill(ROW_X, y + 6, 3, h - 12, C_ACCENT);
      {
         uint16_t lc = r->disabled ? C_DIM : (r->kind == ROW_ACTION && r->danger ? C_BAD : C_TEXT);
         int maxw = r->kind == ROW_SLIDER ? SLIDER_X0 - ROW_X - 22 :
                    r->kind == ROW_CHOICE ? ROW_W - CHOICE_W - 26 : ROW_W - 60;
         ui_text_vc(&ui_font_body, ROW_X + 12, y, h, r->label, lc, maxw);
      }
      switch (r->kind)
      {
         case ROW_ACTION:
            ui_arrow(VAL_R - 4, y + h / 2, 4, 1, sel ? C_ACCENT : C_FAINT);
            if (r->value)
               ui_text_right(&ui_font_small, VAL_R - 14, y + (h - ui_font_small.ascent) / 2, r->value, C_DIM);
            break;
         case ROW_CHOICE:
            draw_choice(VAL_R + 4, y, h, r->value ? r->value : "", sel, r->disabled);
            break;
         case ROW_TOGGLE:
            draw_toggle(VAL_R, y + h / 2, r->on, r->disabled);
            break;
         case ROW_SLIDER:
            draw_slider(y, h, r, sel);
            break;
         case ROW_INFO:
            ui_text_right(&ui_font_body, VAL_R, y + (h - ui_font_body.ascent) / 2, r->value ? r->value : "", C_DIM);
            break;
      }
   }

   /* scroll indicator */
   if (m->nrows > MENU_ROWS_VIS)
   {
      int track = MENU_ROWS_VIS * MENU_ROW_H - 6;
      int bar = track * MENU_ROWS_VIS / m->nrows;
      int pos = (track - bar) * m->scroll / (m->nrows - MENU_ROWS_VIS);
      ui_rrect(UI_W - 4, MENU_LIST_Y + pos, 3, bar, 1, C_FAINT);
   }

   draw_help(MENU_FOOT_Y, UI_H - MENU_FOOT_Y, m->help, m->note, m->note_ok);
}

ui_hit scr_menu_hit(const ui_menu *m, int x, int y)
{
   ui_hit h = { HIT_NONE, 0, 0, ZONE_LABEL, 0 };
   if (y < MENU_TAB_H)
   {
      h.what = HIT_TAB;
      h.tab = x / (UI_W / m->ntabs);
      if (h.tab >= m->ntabs) h.tab = m->ntabs - 1;
      return h;
   }
   if (y >= MENU_LIST_Y && y < MENU_LIST_Y + MENU_ROWS_VIS * MENU_ROW_H)
   {
      int idx = m->scroll + (y - MENU_LIST_Y) / MENU_ROW_H;
      const ui_row *r;
      if (idx >= m->nrows)
         return h;
      r = &m->rows[idx];
      if (r->kind == ROW_HEADER)
         return h;
      h.what = HIT_ROW;
      h.row = idx;
      if (r->kind == ROW_CHOICE && x >= VAL_R + 4 - CHOICE_W)
         h.zone = x < VAL_R + 4 - CHOICE_W / 2 ? ZONE_DEC : ZONE_INC;
      else if (r->kind == ROW_TOGGLE)
         h.zone = ZONE_TOGGLE;
      else if (r->kind == ROW_SLIDER && x >= SLIDER_X0 - 12 && x <= SLIDER_X1 + 12)
      {
         int span = r->vmax - r->vmin;
         int xx = x < SLIDER_X0 ? SLIDER_X0 : x > SLIDER_X1 ? SLIDER_X1 : x;
         h.zone = ZONE_SLIDER;
         h.slider_v = r->vmin + ((xx - SLIDER_X0) * span + (SLIDER_X1 - SLIDER_X0) / 2) / (SLIDER_X1 - SLIDER_X0);
      }
   }
   return h;
}

/* ---- game list ---- */

static void icon_folder(int x, int y, uint16_t c)
{
   ui_rrect(x, y + 2, 7, 4, 1, c);
   ui_rrect(x, y + 4, 16, 11, 2, c);
}

static void icon_cart(int x, int y, uint16_t c)
{
   ui_rrect(x + 1, y, 14, 15, 2, c);
   ui_fill(x + 4, y + 3, 8, 5, C_BG);
   ui_fill(x + 3, y + 11, 10, 1, C_BG);
}

void scr_browser(const char *path, const char *const *names, const unsigned char *isdir,
                 int n, int sel, int top)
{
   int i;
   ui_clear(C_BG);
   ui_vgradient(0, 0, UI_W, 36, C_BG2, C_BG);
   ui_text(&ui_font_title, 10, 3, "gpSP 3D", C_TEXT, 0);
   ui_rrect(92, 8, 30, 15, 7, C_ACCENT2);
   ui_text_center(&ui_font_small, 107, 9, "3D", C_TEXT);
   ui_text_right(&ui_font_small, UI_W - 10, 10, "v" GPSP3D_VERSION, C_DIM);
   ui_text(&ui_font_small, 10, 24, path, C_FAINT, UI_W - 20);

   if (!n)
      ui_text_center(&ui_font_body, UI_W / 2, 110, "No GBA games in this folder", C_DIM);
   for (i = 0; i < LIST_ROWS && top + i < n; i++)
   {
      int y = LIST_Y + i * LIST_ROW_H, idx = top + i, h = LIST_ROW_H - 2;
      int s = idx == sel;
      if (s)
      {
         ui_rrect(ROW_X, y, ROW_W, h, 7, C_ROW_SEL);
         ui_fill(ROW_X, y + 5, 3, h - 10, C_ACCENT);
      }
      if (isdir[idx])
         icon_folder(ROW_X + 10, y + 3, s ? C_WARN : UI_RGB(0xB0, 0x8A, 0x45));
      else
         icon_cart(ROW_X + 10, y + 3, s ? C_ACCENT : C_FAINT);
      ui_text_vc(&ui_font_body, ROW_X + 34, y, h, names[idx], s ? C_TEXT : UI_RGB(0xC5, 0xCB, 0xD6), ROW_W - 50);
   }
   if (n > LIST_ROWS)
   {
      int track = LIST_ROWS * LIST_ROW_H - 4;
      int bar = track * LIST_ROWS / n;
      if (bar < 12) bar = 12;
      ui_rrect(UI_W - 4, LIST_Y + (track - bar) * top / (n - LIST_ROWS), 3, bar, 1, C_FAINT);
   }
   ui_fill(0, 214, UI_W, 26, C_BG2);
   ui_text_vc(&ui_font_small, 10, 214, 26, "A or tap: open     B: back     START: quit", C_DIM, 0);
}

int scr_browser_hit(int y, int top, int n)
{
   int idx;
   if (y < LIST_Y || y >= LIST_Y + LIST_ROWS * LIST_ROW_H)
      return -1;
   idx = top + (y - LIST_Y) / LIST_ROW_H;
   return idx < n ? idx : -1;
}

/* ---- in-game panel ---- */

void scr_hud(const ui_hud *h)
{
   char buf[48];
   int i;
   ui_clear(C_BG);
   ui_vgradient(0, 0, UI_W, 34, C_BG2, C_BG);
   ui_text(&ui_font_bold, 10, 8, h->title, C_TEXT, UI_W - 110);
   ui_text_right(&ui_font_small, UI_W - 10, 11, "gpSP 3D v" GPSP3D_VERSION, C_FAINT);

   /* speed card */
   ui_rrect(8, 40, 148, 70, 12, C_PANEL);
   snprintf(buf, sizeof(buf), "%.0f", h->fps);
   {
      int x = ui_text(&ui_font_big, 20, 48, buf, h->fps >= 58.5f ? C_GOOD : (h->fps >= 50 ? C_WARN : C_BAD), 0);
      ui_text(&ui_font_body, x + 4, 63, "fps", C_DIM, 0);
   }
   snprintf(buf, sizeof(buf), "picture %.0f/s", h->pictures);
   ui_text(&ui_font_small, 20, 88, buf, C_DIM, 0);

   /* 3D card */
   ui_rrect(164, 40, 148, 70, 12, C_PANEL);
   if (h->stereo)
   {
      ui_rrect(176, 52, 34, 18, 9, C_ACCENT2);
      ui_text_center(&ui_font_bold, 193, 52, "3D", C_TEXT);
      snprintf(buf, sizeof(buf), "depth %d%%", h->depth_pct);
      ui_text(&ui_font_body, 218, 53, buf, C_TEXT, 90);
      ui_text(&ui_font_small, 176, 84, "Use the 3D slider", C_DIM, 130);
   }
   else
   {
      ui_rrect(176, 52, 34, 18, 9, C_TRACK);
      ui_text_center(&ui_font_bold, 193, 52, "2D", C_DIM);
      ui_text(&ui_font_small, 176, 84, "Raise the 3D slider", C_DIM, 130);
   }

   if (h->show_stats)
   {
      ui_rrect(8, 118, UI_W - 16, 18 + h->nstats * 15, 10, C_PANEL);
      for (i = 0; i < h->nstats; i++)
      {
         int y = 126 + i * 15;
         ui_text(&ui_font_small, 20, y, h->stat_label[i], C_DIM, 0);
         ui_text(&ui_font_small, 96, y, h->stat_value[i], C_TEXT, UI_W - 112);
      }
   }
   if (h->warning)
      ui_text_center(&ui_font_small, UI_W / 2, 200, h->warning, C_WARN);

   ui_fill(0, 216, UI_W, 24, C_BG2);
   {
      const char *t = "Tap the screen for the menu";
      int w = ui_text_w(&ui_font_body, t), x = (UI_W - w) / 2 + 7;
      ui_circle(x - 12, 228, 4, C_ACCENT);
      ui_text(&ui_font_body, x, 219, t, C_DIM, 0);
   }
}

void scr_message(const char *title, const char *body, const char *hint)
{
   int y = 60, lines = 0;
   const char *p;
   ui_clear(C_BG);
   for (p = body; p && *p; p++)
      if (*p == '\n') lines++;
   ui_rrect(16, 40, UI_W - 32, 110 + lines * 16, 14, C_PANEL);
   ui_text(&ui_font_title, 32, 52, title, C_TEXT, UI_W - 64);
   y = 84;
   if (body)
   {
      char line[128];
      const char *s = body;
      while (*s)
      {
         const char *nl = strchr(s, '\n');
         int n = nl ? (int)(nl - s) : (int)strlen(s);
         if (n > 127) n = 127;
         memcpy(line, s, n);
         line[n] = 0;
         ui_text(&ui_font_body, 32, y, line, C_DIM, UI_W - 64);
         y += 17;
         s += n + (nl ? 1 : 0);
         if (!nl) break;
      }
   }
   if (hint)
   {
      ui_fill(0, 216, UI_W, 24, C_BG2);
      ui_text_center(&ui_font_body, UI_W / 2, 219, hint, C_ACCENT);
   }
}
