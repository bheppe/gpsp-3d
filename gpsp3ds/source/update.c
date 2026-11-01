/* Update check: once at startup, in the background, asks GitHub which
 * release is the latest. github.com/<repo>/releases/latest answers with a
 * redirect to .../releases/tag/<tag>, so only that redirect is read (no
 * JSON, no download). Skipped quietly without Wi-Fi.
 *
 * With libcurl (devkitPro's 3ds-curl, found by gpsp3d.mk) it goes through
 * curl + mbedTLS, like Universal-Updater: the 3DS's own HTTPS (httpc) can't
 * talk to github.com any more (its TLS is too old for GitHub's certificate).
 * Without it, httpc is tried anyway. */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "update.h"
#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

#define LATEST_URL "https://github.com/bheppe/gpsp-3d/releases/latest"
#define USER_AGENT "gpSP-3D-update-check"

static Thread         upd_thr;
static volatile bool  upd_running, upd_cancel;
static volatile int   upd_state;          /* UPD_* */
static char           upd_latest[24];
static char           upd_error[64];
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

/* .../releases/tag/v0.1.7 -> "0.1.7" */
static bool take_tag(const char *loc)
{
   const char *t = loc ? strstr(loc, "/tag/") : NULL;
   size_t n;
   if (!t)
      return false;
   t += 5;
   if (*t == 'v' || *t == 'V')
      t++;
   n = strcspn(t, "/?#\r\n");
   if (n == 0 || n >= sizeof(upd_latest))
      return false;
   memcpy(upd_latest, t, n);
   upd_latest[n] = 0;
   upd_state = ver_cmp(upd_latest, upd_current) > 0 ? UPD_NEWER : UPD_CURRENT;
   return true;
}

#ifdef HAVE_CURL
#define SOC_SIZE 0x40000
static int xfer_cb(void *p, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d)
{
   (void)p; (void)a; (void)b; (void)c; (void)d;
   return upd_cancel;   /* non-zero aborts */
}

static size_t sink_cb(char *p, size_t size, size_t n, void *u)
{
   (void)p; (void)u;
   return size * n;   /* the page itself isn't needed */
}

static void check_curl(void)
{
   u32 *soc = (u32*)memalign(0x1000, SOC_SIZE);
   CURL *c;
   Result r;
   if (!soc)
   {
      snprintf(upd_error, sizeof(upd_error), "no memory for the network");
      return;
   }
   if (R_FAILED(r = socInit(soc, SOC_SIZE)))
   {
      snprintf(upd_error, sizeof(upd_error), "network start failed (%08lX)", (unsigned long)r);
      free(soc);
      return;
   }
   curl_global_init(CURL_GLOBAL_DEFAULT);
   c = curl_easy_init();
   if (c)
   {
      CURLcode e;
      long code = 0;
      char *loc = NULL;
      curl_easy_setopt(c, CURLOPT_URL, LATEST_URL);
      curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
      curl_easy_setopt(c, CURLOPT_NOBODY, 1L);            /* HEAD: only the redirect */
      curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
      curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);    /* no CA list on the 3DS */
      curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
      curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
      curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
      curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink_cb);
      curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
      curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xfer_cb);
      e = curl_easy_perform(c);
      if (e == CURLE_OK)
      {
         curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
         curl_easy_getinfo(c, CURLINFO_REDIRECT_URL, &loc);
         if (!take_tag(loc))
            snprintf(upd_error, sizeof(upd_error), "unexpected answer (HTTP %ld)", code);
      }
      else
         snprintf(upd_error, sizeof(upd_error), "%.50s", curl_easy_strerror(e));
      curl_easy_cleanup(c);
   }
   curl_global_cleanup();
   socExit();
   free(soc);
}
#endif

static void check_httpc(void)
{
   static httpcContext ctx;
   char loc[256] = "";
   u32 code = 0;
   Result r;
   if (R_FAILED(r = httpcInit(0)))
   {
      snprintf(upd_error, sizeof(upd_error), "httpc start failed (%08lX)", (unsigned long)r);
      return;
   }
   if (R_SUCCEEDED(r = httpcOpenContext(&ctx, HTTPC_METHOD_GET, LATEST_URL, 0)))
   {
      httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
      httpcSetKeepAlive(&ctx, HTTPC_KEEPALIVE_DISABLED);
      httpcAddRequestHeaderField(&ctx, "User-Agent", USER_AGENT);
      if (R_FAILED(r = httpcBeginRequest(&ctx)))
         snprintf(upd_error, sizeof(upd_error), "httpc request failed (%08lX)", (unsigned long)r);
      else if (R_FAILED(r = httpcGetResponseStatusCodeTimeout(&ctx, &code, 10000000000ULL)))
         snprintf(upd_error, sizeof(upd_error), "httpc no answer (%08lX)", (unsigned long)r);
      else if (code < 300 || code >= 400 ||
               R_FAILED(httpcGetResponseHeader(&ctx, "Location", loc, sizeof(loc) - 1)) ||
               !take_tag(loc))
         snprintf(upd_error, sizeof(upd_error), "unexpected answer (HTTP %lu)", (unsigned long)code);
      httpcCloseContext(&ctx);
   }
   else
      snprintf(upd_error, sizeof(upd_error), "httpc open failed (%08lX)", (unsigned long)r);
   httpcExit();
}

static void upd_main(void *arg)
{
   (void)arg;
   if (!wifi_up())
      upd_state = UPD_OFFLINE;
   else
   {
      upd_state = UPD_CHECKING;
#ifdef HAVE_CURL
      check_curl();
#else
      check_httpc();
#endif
      if (upd_state == UPD_CHECKING)
         upd_state = UPD_FAILED;
   }
   __dmb();
   upd_running = false;
}

void update_check_start(const char *current_version)
{
   if (upd_running)
      return;
   if (upd_thr)
   {
      threadJoin(upd_thr, U64_MAX);
      threadFree(upd_thr);
      upd_thr = NULL;
   }
   upd_current = current_version;
   upd_error[0] = 0;
   upd_cancel = false;
   upd_state = UPD_CHECKING;
   upd_running = true;
   /* lowest priority, on the main core: it only waits on the network */
   upd_thr = threadCreate(upd_main, NULL, 32 * 1024, 0x3F, 0, false);
   if (!upd_thr)
   {
      upd_running = false;
      upd_state = UPD_FAILED;
      snprintf(upd_error, sizeof(upd_error), "couldn't start the check");
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

const char *update_error(void)
{
   return upd_error;
}

const char *update_method(void)
{
#ifdef HAVE_CURL
   return "curl";
#else
   return "httpc";
#endif
}

void update_check_stop(void)
{
   if (!upd_thr)
      return;
   upd_cancel = true;
   threadJoin(upd_thr, 2000000000ULL);   /* 2 s at most: never hold up closing */
   if (!upd_running)
      threadFree(upd_thr);
   upd_thr = NULL;
}
