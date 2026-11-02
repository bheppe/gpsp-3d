/* PC test for boxart: gets a game's box art as Icon Maker does (libretro's
 * game list cached in list.txt, the libraries' in list.txt.1, .2, ...; the
 * picture shrunk to a 128-pixel short side), from the libraries given
 * (URLs ending in /) first, then libretro:
 *   boxarttest game.gba out.png [list.txt [library-url ...]]
 * prints the name found, where from and the time, or why it couldn't. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "../source/boxart.h"
#include "../source/netget.h"

static double now(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
   const char *dot, *list_path = argc > 3 ? argv[3] : "list.txt";
   char found[256], err[200], cache[300];
   boxart_source src[9];
   double t0;
   int sys, r, n = 0, i, from;
   if (argc < 3) { fprintf(stderr, "usage: boxarttest game.gba out.png [list.txt [library-url ...]]\n"); return 2; }
   dot = strrchr(argv[1], '.');
   sys = dot && (!strcasecmp(dot, ".sfc") || !strcasecmp(dot, ".smc")) ? BOXART_SNES : BOXART_GBA;
   if (dot && !strcasecmp(dot, ".zip") && strstr(argv[1], "snes"))
      sys = BOXART_SNES;
   net_begin();
   for (i = 4; i < argc && n < 8; i++)
      src[n++].base = argv[i];
   src[n++].base = NULL;
   for (i = 0; i < n; i++)
   {
      if (src[i].base)
         snprintf(cache, sizeof(cache), "%s.%d", list_path, i + 1);
      else
         snprintf(cache, sizeof(cache), "%s", list_path);
      t0 = now();
      if (boxart_list_load(&src[i].list, src[i].base, sys, cache, err, sizeof(err)))
      {
         printf("game list of %s failed: %s\n", src[i].base ? src[i].base : "libretro", err);
         if (!src[i].base)
            return 1;
         /* a library: left out, as Icon Maker does */
         memmove(src + i, src + i + 1, (n - i - 1) * sizeof(*src));
         n--;
         i--;
         continue;
      }
      printf("game list of %s: %d games, %.2f s\n", src[i].base ? src[i].base : "libretro", src[i].list.n, now() - t0);
   }
   t0 = now();
   r = boxart_get(src, n, argv[1], sys, argv[2], 128, found, sizeof(found), &from, err, sizeof(err));
   printf("%s: %s%s%s%s%s, %.2f s\n", argv[1],
          r == BOXART_SAVED ? "saved " : r == BOXART_NONE ? "no box art" : r == BOXART_STOPPED ? "stopped" : "error: ",
          r == BOXART_SAVED ? found : "", r == BOXART_SAVED ? " from " : "",
          r == BOXART_SAVED ? (src[from].base ? src[from].base : "libretro") : "", r == BOXART_ERROR ? err : "",
          now() - t0);
   for (i = 0; i < n; i++)
      boxart_list_free(&src[i].list);
   net_end();
   return r == BOXART_SAVED ? 0 : 1;
}
