/* Files over HTTPS into memory: see netget.h. As gpSP 3D's update check
 * (update.c), but whole files. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "netget.h"

#define USER_AGENT "gpSP-3D-Icon-Maker"

int (*net_stop)(void);
void (*net_progress)(size_t got, size_t total);

static CURL *curl;

typedef struct { uint8_t *p; size_t n, room, max; int too_big; } sink;

static size_t on_data(char *p, size_t size, size_t n, void *u)
{
   sink *s = u;
   size_t len = size * n;
   if (s->n + len > s->max)
   {
      s->too_big = 1;
      return 0;                      /* stops the transfer */
   }
   if (s->n + len + 1 > s->room)
   {
      size_t room = s->room ? s->room : 64 * 1024;
      uint8_t *q;
      while (room < s->n + len + 1)
         room *= 2;
      if (!(q = realloc(s->p, room)))
         return 0;
      s->p = q;
      s->room = room;
   }
   memcpy(s->p + s->n, p, len);
   s->n += len;
   return len;
}

static int on_progress(void *u, curl_off_t total, curl_off_t got, curl_off_t ut, curl_off_t ug)
{
   (void)u; (void)ut; (void)ug;
   if (net_progress)
      net_progress((size_t)got, (size_t)total);
   return net_stop && net_stop();    /* nonzero: stop */
}

void net_begin(void)
{
   if (curl)
      return;
   curl_global_init(CURL_GLOBAL_DEFAULT);
   curl = curl_easy_init();
}

void net_end(void)
{
   if (!curl)
      return;
   curl_easy_cleanup(curl);
   curl_global_cleanup();
   curl = NULL;
}

int net_get(const char *url, size_t max, uint8_t **data, size_t *size, char *err, int errlen)
{
   sink s = { NULL, 0, 0, max, 0 };
   CURLcode e;
   long code = 0;
   *data = NULL;
   *size = 0;
   if (!curl)
   {
      snprintf(err, errlen, "network not started");
      return -1;
   }
   curl_easy_reset(curl);
   curl_easy_setopt(curl, CURLOPT_URL, url);
   curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
   curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
   curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);   /* no CA list on the 3DS */
   curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
   curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
   /* no limit on the whole download (the game list is ~1 MB on slow
    * Wi-Fi), but give up if it stalls */
   curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 256L);
   curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
   curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
   curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
   curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_progress);
   e = curl_easy_perform(curl);
   if (e != CURLE_OK)
   {
      if (s.too_big)
         snprintf(err, errlen, "file too big");
      else if (e == CURLE_ABORTED_BY_CALLBACK)
         snprintf(err, errlen, "stopped");
      else
         snprintf(err, errlen, "%.60s", curl_easy_strerror(e));
      free(s.p);
      return -1;
   }
   curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
   if (!s.p && !(s.p = malloc(1)))
   {
      snprintf(err, errlen, "out of memory");
      return -1;
   }
   s.p[s.n] = 0;
   *data = s.p;
   *size = s.n;
   return (int)code;
}
