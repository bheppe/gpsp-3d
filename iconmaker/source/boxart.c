/* Box art from libretro's thumbnails: see boxart.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <png.h>
#include <zlib.h>
#include "boxart.h"
#include "netget.h"
#include "pngshrink.h"

#define LIST_URL   "https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/"
/* the thumbnails' own repositories (thumbnails.libretro.com is made from
 * them): the same server as the list, so one connection does it all */
#define THUMBS_URL "https://raw.githubusercontent.com/libretro-thumbnails/"
#define LIST_MAX   (8 << 20)
#define PIC_MAX    (4 << 20)
#define TRIES      6                 /* names tried at most for one game */
#define NAME_MAX_  256

/* the system's name in libretro's list, and its thumbnails' repository */
static const char *const sys_name[BOXART_SYSTEMS] = {
   "Nintendo - Game Boy Advance",
   "Nintendo - Super Nintendo Entertainment System",
};
static const char *const sys_repo[BOXART_SYSTEMS] = {
   "Nintendo_-_Game_Boy_Advance",
   "Nintendo_-_Super_Nintendo_Entertainment_System",
};

int (*boxart_stop)(void);
void (*boxart_progress)(int step, size_t done, size_t total);

static int step, stopped;

static int on_stop(void)
{
   if (boxart_stop && boxart_stop())
      stopped = 1;
   return stopped;
}

static void on_progress(size_t got, size_t total)
{
   if (boxart_progress)
      boxart_progress(step, got, total);
}

/* s with everything but letters, digits and -._~ as %XX */
static void url_escape(const char *s, char *out, int outlen)
{
   static const char hex[] = "0123456789ABCDEF";
   int n = 0;
   for (; *s && n + 4 < outlen; s++)
   {
      unsigned char c = (unsigned char)*s;
      if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
          c == '-' || c == '.' || c == '_' || c == '~')
         out[n++] = (char)c;
      else
      {
         out[n++] = '%';
         out[n++] = hex[c >> 4];
         out[n++] = hex[c & 15];
      }
   }
   out[n] = 0;
}

/* ---- the game list ---- */

static int cmp_crc(const void *a, const void *b)
{
   uint32_t x = ((const boxart_game*)a)->crc, y = ((const boxart_game*)b)->crc;
   return x < y ? -1 : x > y;
}

/* text ("CRC32 name" lines) -> l->g, in place; l->text owns it */
static int parse_list(boxart_list *l, char *text)
{
   char *p = text;
   int n = 0, room = 0;
   l->text = text;
   l->g = NULL;
   l->n = 0;
   while (*p)
   {
      char *end = strchr(p, '\n'), *sp = strchr(p, ' ');
      if (end)
         *end = 0;
      if (sp && (!end || sp < end) && sp - p == 8)
      {
         if (n == room)
         {
            boxart_game *g = realloc(l->g, (room = room ? room * 2 : 4096) * sizeof(*g));
            if (!g)
               return -1;
            l->g = g;
         }
         l->g[n].crc = (uint32_t)strtoul(p, NULL, 16);
         l->g[n].name = sp + 1;
         n++;
      }
      if (!end)
         break;
      p = end + 1;
   }
   l->n = n;
   qsort(l->g, n, sizeof(*l->g), cmp_crc);
   return 0;
}

static char *read_file(const char *path)
{
   FILE *f = fopen(path, "rb");
   char *text = NULL;
   long size;
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);
   if (size > 0 && size < LIST_MAX && (text = malloc(size + 1)))
   {
      if (fread(text, 1, size, f) == (size_t)size)
         text[size] = 0;
      else
      {
         free(text);
         text = NULL;
      }
   }
   fclose(f);
   return text;
}

/* libretro's list (clrmamepro: game ( name "..." ... rom ( ... crc XXXXXXXX ... ) ))
 * -> "CRC32 name" lines */
static char *list_from_dat(char *dat)
{
   size_t room = 256 * 1024, n = 0;
   char *out = malloc(room), *p = dat;
   if (!out)
      return NULL;
   while ((p = strstr(p, "\ngame (")))
   {
      char *next = strstr(p + 7, "\ngame ("), *name, *q, *crc;
      size_t len;
      if (next)
         *next = 0;                  /* the searches below stay in this game */
      name = strstr(p, "\tname \"");
      crc = strstr(p, " crc ");
      if (name && crc && strlen(crc) >= 13)
      {
         name += 7;
         q = strchr(name, '"');
         len = q ? (size_t)(q - name) : 0;
         if (len && len < NAME_MAX_)
         {
            if (n + len + 11 > room)
            {
               char *o = realloc(out, room *= 2);
               if (!o)
               {
                  free(out);
                  return NULL;
               }
               out = o;
            }
            memcpy(out + n, crc + 5, 8);
            out[n + 8] = ' ';
            memcpy(out + n + 9, name, len);
            out[n + 9 + len] = '\n';
            n += len + 10;
         }
      }
      if (!next)
         break;
      *next = '\n';
      p = next;
   }
   out[n] = 0;
   return out;
}

static void write_list(const boxart_list *l, const char *cache)
{
   /* parse_list() cut the lines: written out line by line */
   FILE *f = fopen(cache, "w");
   int i;
   if (f)
   {
      for (i = 0; i < l->n; i++)
         fprintf(f, "%08lX %s\n", (unsigned long)l->g[i].crc, l->g[i].name);
      fclose(f);
   }
}

int boxart_list_load(boxart_list *l, const char *base, int sys, const char *cache, char *err, int errlen)
{
   char url[600], esc[200], *text;
   uint8_t *dat;
   size_t size;
   int code;
   memset(l, 0, sizeof(*l));
   if ((text = read_file(cache)))
   {
      if (parse_list(l, text) == 0 && l->n)
         return 0;
      boxart_list_free(l);
   }
   url_escape(sys_name[sys], esc, sizeof(esc));
   if (base)
      snprintf(url, sizeof(url), "%s%s/list.txt", base, sys_repo[sys]);
   else
      snprintf(url, sizeof(url), LIST_URL "%s.dat", esc);
   step = BOXART_STEP_LIST;
   stopped = 0;
   net_stop = on_stop;
   net_progress = on_progress;
   code = net_get(url, LIST_MAX, &dat, &size, err, errlen);
   net_stop = NULL;
   net_progress = NULL;
   if (base && code == 404)        /* a library without a list: by file name */
   {
      free(dat);
      return 0;
   }
   if (code != 200)
   {
      if (code > 0)
         snprintf(err, errlen, "game list: HTTP %d", code);
      free(dat);
      return stopped ? BOXART_STOPPED : BOXART_ERROR;
   }
   if (base)
   {
      /* already "CRC32 name" lines */
      if (parse_list(l, (char*)dat))
      {
         snprintf(err, errlen, "out of memory");
         boxart_list_free(l);
         return BOXART_ERROR;
      }
      write_list(l, cache);
      return 0;
   }
   text = list_from_dat((char*)dat);
   free(dat);
   if (!text || parse_list(l, text) || !l->n)
   {
      snprintf(err, errlen, text ? "game list: no games in it" : "out of memory");
      if (!l->text)
         free(text);
      boxart_list_free(l);
      return BOXART_ERROR;
   }
   write_list(l, cache);
   return 0;
}

void boxart_list_free(boxart_list *l)
{
   free(l->text);
   free(l->g);
   memset(l, 0, sizeof(*l));
}

/* ---- which game it is ---- */

/* the file name without folder and ending */
static void stem_of(const char *path, char *out, int outlen)
{
   const char *name = strrchr(path, '/'), *dot;
   name = name ? name + 1 : path;
   dot = strrchr(name, '.');
   snprintf(out, outlen, "%.*s", dot ? (int)(dot - name) : (int)strlen(name), name);
}

static const char *by_name(const boxart_list *l, const char *name)
{
   int i;
   for (i = 0; i < l->n; i++)
      if (!strcasecmp(l->g[i].name, name))
         return l->g[i].name;
   return NULL;
}

static const char *by_crc(const boxart_list *l, uint32_t crc)
{
   boxart_game key, *g;
   key.crc = crc;
   g = bsearch(&key, l->g, l->n, sizeof(*l->g), cmp_crc);
   while (g && g > l->g && g[-1].crc == crc)
      g--;
   return g ? g->name : NULL;
}

static uint32_t le32(const uint8_t *p)
{
   return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* A zip's first file: its CRC32 and name, from the zip's own index (the
 * central directory at the end), so nothing needs unpacking. 0 if it isn't
 * a zip that can be read. */
static int zip_first(FILE *f, uint32_t *crc, char *name, int namelen)
{
   uint8_t buf[1024], *eocd = NULL, cd[46];
   long size, at;
   int n, i, len;
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   at = size > (long)sizeof(buf) ? size - (long)sizeof(buf) : 0;
   fseek(f, at, SEEK_SET);
   n = (int)fread(buf, 1, sizeof(buf), f);
   for (i = n - 22; i >= 0 && !eocd; i--)
      if (!memcmp(buf + i, "PK\5\6", 4))
         eocd = buf + i;
   if (!eocd)
      return 0;
   fseek(f, (long)le32(eocd + 16), SEEK_SET);
   if (fread(cd, 1, 46, f) != 46 || memcmp(cd, "PK\1\2", 4))
      return 0;
   *crc = le32(cd + 16);
   len = cd[28] | cd[29] << 8;
   if (len >= namelen)
      len = namelen - 1;
   if (fread(name, 1, len, f) != (size_t)len)
      return 0;
   name[len] = 0;
   return 1;
}

/* CRC32 of the whole file (a SNES copier's 512-byte header left out, as
 * No-Intro does). 0 and *crc, or -1 (can't read it, or stopped). */
static int file_crc(FILE *f, int sys, uint32_t *crc)
{
   enum { CHUNK = 256 * 1024 };
   uint8_t *buf = malloc(CHUNK);
   long size, done = 0;
   uLong c = crc32(0, NULL, 0);
   size_t n;
   if (!buf)
      return -1;
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   if (sys == BOXART_SNES && size % 1024 == 512)
      done = 512;
   fseek(f, done, SEEK_SET);
   step = BOXART_STEP_READ;
   while ((n = fread(buf, 1, CHUNK, f)) > 0)
   {
      c = crc32(c, buf, (uInt)n);
      done += (long)n;
      if (boxart_progress)
         boxart_progress(step, (size_t)done, (size_t)size);
      if (on_stop())
         break;
   }
   free(buf);
   *crc = (uint32_t)c;
   return stopped || done != size ? -1 : 0;
}

/* how good a name is to try after the game's own: other regions, then
 * betas, demos and the like (which rarely have box art) */
static int rank(const char *name)
{
   static const char *const order[] = { "(USA", "(World", "(Europe", "(Japan" };
   static const char *const poor[] = { "(Beta", "(Proto", "(Demo", "(Sample", "(Kiosk",
                                       "(Virtual Console", "(Unl", "(Pirate" };
   int i;
   for (i = 0; i < (int)(sizeof(poor) / sizeof(*poor)); i++)
      if (strstr(name, poor[i]))
         return 9;
   for (i = 0; i < (int)(sizeof(order) / sizeof(*order)); i++)
      if (strstr(name, order[i]))
         return i;
   return 4;
}

/* the title: the name without " (USA)", " (Rev 1)" and so on */
static int title_len(const char *name)
{
   const char *p = strstr(name, " (");
   return p ? (int)(p - name) : (int)strlen(name);
}

typedef struct { char name[TRIES][NAME_MAX_]; int n; } tries;

static void try_add(tries *t, const char *name)
{
   int i;
   if (t->n == TRIES || !name || !*name)
      return;
   for (i = 0; i < t->n; i++)
      if (!strcmp(t->name[i], name))
         return;
   snprintf(t->name[t->n++], NAME_MAX_, "%s", name);
}

/* the list's other names with the same title as name, best first */
static void try_same_title(const boxart_list *l, tries *t, const char *name)
{
   int len = title_len(name), r, i;
   for (r = 0; r <= 9 && t->n < TRIES; r++)
      for (i = 0; i < l->n && t->n < TRIES; i++)
         if (title_len(l->g[i].name) == len && !strncasecmp(l->g[i].name, name, len) &&
             rank(l->g[i].name) == r)
            try_add(t, l->g[i].name);
}

/* The game: its file name, and for a zip its first file's name and CRC32
 * (from the zip's index); any other game's CRC32 only once a list needs it
 * (crc_state 0: not read yet, 1: crc, -1: can't be read) */
typedef struct
{
   const char *path;
   int sys, crc_state;
   uint32_t crc;
   char stem[NAME_MAX_], istem[NAME_MAX_];
} game_id;

static void id_init(game_id *id, const char *path, int sys)
{
   const char *dot = strrchr(path, '.');
   char inner[NAME_MAX_];
   FILE *f;
   memset(id, 0, sizeof(*id));
   id->path = path;
   id->sys = sys;
   stem_of(path, id->stem, sizeof(id->stem));
   if (dot && !strcasecmp(dot, ".zip"))
   {
      id->crc_state = -1;
      if ((f = fopen(path, "rb")))
      {
         if (zip_first(f, &id->crc, inner, sizeof(inner)))
         {
            stem_of(inner, id->istem, sizeof(id->istem));
            id->crc_state = 1;
         }
         fclose(f);
      }
   }
}

/* 0, or -1 if stopped */
static int id_crc(game_id *id)
{
   FILE *f;
   if (id->crc_state)
      return 0;
   id->crc_state = -1;
   if ((f = fopen(id->path, "rb")))
   {
      setvbuf(f, NULL, _IOFBF, 64 * 1024);
      if (file_crc(f, id->sys, &id->crc) == 0)
         id->crc_state = 1;
      fclose(f);
   }
   return stopped ? -1 : 0;
}

/* the names to try for the game in list l; -1 if stopped */
static int names_for(const boxart_list *l, game_id *id, tries *t)
{
   const char *own = NULL, *stem = id->stem;
   t->n = 0;
   if (l->n)
   {
      own = by_name(l, stem);
      if (!own && id->istem[0])
         own = by_name(l, id->istem);
      if (!own && id_crc(id))
         return -1;
      if (!own && id->crc_state == 1)
         own = by_crc(l, id->crc);
   }
   if (own)
   {
      try_add(t, own);
      try_same_title(l, t, own);
   }
   else
   {
      /* not in the list: perhaps a newer game with a No-Intro name, or
       * one named without its region */
      try_add(t, stem);
      try_same_title(l, t, stem);
   }
   return 0;
}

/* ---- the picture ---- */

static int write_png(const char *path, const uint8_t *rgba, int w, int h)
{
   png_structp png;
   png_infop info;
   FILE *f;
   int y, x, opaque = 1;
   uint8_t *row = malloc((size_t)w * 4);
   if (!row || !(f = fopen(path, "wb")))
   {
      free(row);
      return -1;
   }
   png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
   info = png ? png_create_info_struct(png) : NULL;
   if (!info || setjmp(png_jmpbuf(png)))
   {
      png_destroy_write_struct(&png, &info);
      fclose(f);
      free(row);
      return -1;
   }
   for (x = 0; x < w * h && opaque; x++)
      opaque = rgba[x * 4 + 3] == 255;
   png_init_io(png, f);
   png_set_IHDR(png, info, w, h, 8, opaque ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGBA,
                PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
   png_write_info(png, info);
   for (y = 0; y < h; y++)
   {
      const uint8_t *p = rgba + (size_t)y * w * 4;
      if (opaque)
         for (x = 0; x < w; x++)
            memcpy(row + x * 3, p + x * 4, 3);
      png_write_row(png, opaque ? row : (png_bytep)p);
   }
   png_write_end(png, NULL);
   png_destroy_write_struct(&png, &info);
   free(row);
   return fclose(f) ? -1 : 0;
}

static int write_raw(const char *path, const uint8_t *data, size_t size)
{
   FILE *f = fopen(path, "wb");
   size_t n;
   if (!f)
      return -1;
   n = fwrite(data, 1, size, f);
   return fclose(f) || n != size ? -1 : 0;
}

/* the downloaded PNG shrunk (pngshrink.c) and written to out: through a
 * .part file, so a failed write leaves no broken picture. As it came if it
 * can't be shrunk (an interlaced PNG). */
static int save_picture(const uint8_t *png, size_t size, int side, const char *out)
{
   char part[600];
   uint8_t *img = NULL;
   int w = 0, h = 0, il, r;
   uint32_t mem;
   FILE *f = fmemopen((void*)png, size, "rb");
   if (f)
   {
      int (*stop)(void) = png_shrink_stop;
      png_shrink_stop = NULL;
      img = png_shrink(f, side, side, 4096, &w, &h, &il, &mem);
      png_shrink_stop = stop;
      fclose(f);
   }
   snprintf(part, sizeof(part), "%s.part", out);
   r = img ? write_png(part, img, w, h) : write_raw(part, png, size);
   free(img);
   remove(out);
   if (r || rename(part, out))
   {
      remove(part);
      return -1;
   }
   return 0;
}

/* the names in t from one source (base NULL: libretro); BOXART_* */
static int get_from(const char *base, int sys, const tries *t, const char *out, int side,
                    char *found, int foundlen, char *err, int errlen)
{
   char esc[3 * NAME_MAX_], name[NAME_MAX_], url[1024];
   int i, k, r = BOXART_NONE;
   for (i = 0; i < t->n && r == BOXART_NONE; i++)
   {
      uint8_t *png;
      size_t size;
      int code;
      /* the server's file names have & * / : ` < > ? \ | " as _ (RetroArch's rule) */
      snprintf(name, sizeof(name), "%s", t->name[i]);
      for (k = 0; name[k]; k++)
         if (strchr("&*/:`<>?\\|\"", name[k]))
            name[k] = '_';
      url_escape(name, esc, sizeof(esc));
      if (base)
         snprintf(url, sizeof(url), "%s%s/Named_Boxarts/%s.png", base, sys_repo[sys], esc);
      else
         snprintf(url, sizeof(url), THUMBS_URL "%s/master/Named_Boxarts/%s.png", sys_repo[sys], esc);
      code = net_get(url, PIC_MAX, &png, &size, err, errlen);
      if (code == 200 && size > 8 && !memcmp(png, "\x89PNG", 4))
      {
         if (save_picture(png, size, side, out))
         {
            snprintf(err, errlen, "could not write the picture");
            r = BOXART_ERROR;
         }
         else
         {
            snprintf(found, foundlen, "%s", t->name[i]);
            r = BOXART_SAVED;
         }
      }
      else if (code < 0)
         r = stopped ? BOXART_STOPPED : BOXART_ERROR;
      free(png);
   }
   return r;
}

int boxart_get(const boxart_source *src, int n, const char *path, int sys, const char *out, int side,
               char *found, int foundlen, int *from, char *err, int errlen)
{
   static tries t;
   static game_id id;
   char e[200];
   int i, r = BOXART_NONE, failed = 0;
   *found = 0;
   *from = -1;
   err[0] = 0;
   stopped = 0;
   id_init(&id, path, sys);
   for (i = 0; i < n && r != BOXART_SAVED && r != BOXART_STOPPED; i++)
   {
      if (names_for(&src[i].list, &id, &t))
      {
         r = BOXART_STOPPED;
         break;
      }
      net_stop = on_stop;
      net_progress = on_progress;
      step = BOXART_STEP_DOWNLOAD;
      r = get_from(src[i].base, sys, &t, out, side, found, foundlen, e, sizeof(e));
      net_stop = NULL;
      net_progress = NULL;
      if (r == BOXART_SAVED)
         *from = i;
      else if (r == BOXART_ERROR && !failed++)
         snprintf(err, errlen, "%s", e);  /* the first failure; on to the next source */
   }
   if (r == BOXART_NONE && failed)
      r = BOXART_ERROR;
   return r;
}
