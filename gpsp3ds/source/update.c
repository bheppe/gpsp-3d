/* Update check: once at startup, in the background, asks GitHub which
 * release is the latest. github.com/<repo>/releases/latest answers with a
 * redirect to .../releases/tag/<tag>, so only the Location header is read
 * (no JSON, no download). Skipped quietly without Wi-Fi or on any error. */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "update.h"

#define LATEST_URL "https://github.com/bheppe/gpsp-3d/releases/latest"

static Thread         upd_thr;
static httpcContext   upd_ctx;
static volatile bool  upd_ctx_open, upd_running;
static volatile int   upd_state;          /* UPD_* */
static char           upd_latest[24];
static const char    *upd_current;

/* "0.1.10" > "0.1.9": compare number by number */
static int ver_cmp(const char *a, const char *b)
{
   while (*a || *b)
   {
      long x = strtol(a, (char**)&a, 10), y = strtol(b, (char**)&b, 10);
      if (x != y)
         return x < y ? -1 : 1;
      if (*a == '.') a++;
      if (*b == '.') b++;
      if ((*a && (*a < '0' || *a > '9')) || (*b && (*b < '0' || *b > '9')))
         break;   /* "-test" and the like: stop at the numbers */
   }
   return 0;
}

static bool wifi_up(void)
{
   u32 st = 0;
   if (R_FAILED(acInit()))
      return false;
   if (R_FAILED(ACU_GetWifiStatus(&st)))
      st = 0;
   acExit();
   return st != 0;
}

static void upd_main(void *arg)
{
   char loc[256] = "";
   u32 code = 0;
   (void)arg;
   upd_state = UPD_OFFLINE;
   if (!wifi_up() || R_FAILED(httpcInit(0)))
      goto out;
   if (R_SUCCEEDED(httpcOpenContext(&upd_ctx, HTTPC_METHOD_GET, LATEST_URL, 0)))
   {
      upd_ctx_open = true;
      upd_state = UPD_FAILED;
      httpcSetSSLOpt(&upd_ctx, SSLCOPT_DisableVerify);   /* the 3DS's own root certificates are out of date */
      httpcSetKeepAlive(&upd_ctx, HTTPC_KEEPALIVE_DISABLED);
      httpcAddRequestHeaderField(&upd_ctx, "User-Agent", "gpSP-3D-update-check");
      if (R_SUCCEEDED(httpcBeginRequest(&upd_ctx)) &&
          R_SUCCEEDED(httpcGetResponseStatusCodeTimeout(&upd_ctx, &code, 10000000000ULL)) &&
          code >= 300 && code < 400 &&
          R_SUCCEEDED(httpcGetResponseHeader(&upd_ctx, "Location", loc, sizeof(loc) - 1)))
      {
         const char *t = strstr(loc, "/tag/");
         if (t)
         {
            size_t n;
            t += 5;
            if (*t == 'v' || *t == 'V')
               t++;
            n = strcspn(t, "/?#\r\n");
            if (n > 0 && n < sizeof(upd_latest))
            {
               memcpy(upd_latest, t, n);
               upd_latest[n] = 0;
               upd_state = ver_cmp(upd_latest, upd_current) > 0 ? UPD_NEWER : UPD_CURRENT;
            }
         }
      }
      upd_ctx_open = false;
      httpcCloseContext(&upd_ctx);
   }
   httpcExit();
out:
   __dmb();
   upd_running = false;
}

void update_check_start(const char *current_version)
{
   if (upd_running || upd_thr)
      return;
   upd_current = current_version;
   upd_state = UPD_CHECKING;
   upd_running = true;
   /* lowest priority, on the main core: it only waits on the network */
   upd_thr = threadCreate(upd_main, NULL, 16 * 1024, 0x3F, 0, false);
   if (!upd_thr)
   {
      upd_running = false;
      upd_state = UPD_FAILED;
   }
}

int update_state(void)
{
   return upd_state;
}

const char *update_latest(void)
{
   return upd_latest;
}

void update_check_stop(void)
{
   if (!upd_thr)
      return;
   if (upd_running && upd_ctx_open)
      httpcCancelConnection(&upd_ctx);
   threadJoin(upd_thr, 2000000000ULL);   /* 2 s at most: never hold up closing */
   if (!upd_running)
      threadFree(upd_thr);
   upd_thr = NULL;
}
