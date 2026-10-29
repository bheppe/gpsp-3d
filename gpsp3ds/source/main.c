/*
 * gpSP 3D - standalone gpSP frontend for Nintendo 3DS
 *
 * Step 1: gpSP's own software renderer, shown 1:1 on the top screen.
 * Runs the gpSP libretro core (statically linked) without RetroArch, so the
 * frontend overhead per frame is only a framebuffer copy and an audio submit.
 *
 * Controls: A/B/L/R/Start/Select/D-pad/Circle Pad as on a GBA.
 *           Touch the bottom screen for the menu.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <citro3d.h>
#include <math.h>
#include "libretro.h"
#include "ctr-gpu.h"
#include "ui.h"
#include "ui_screens.h"
#include "unzip.h"
#include "guide.h"

/* gpSP takes up to 32 MB of the normal heap for ROM blocks, so keep the linear
 * heap (only needed for framebuffers and audio) small. */
u32 __ctru_linear_heap_size = 6 * 1024 * 1024;
u32 __stacksize__           = 1024 * 1024;

/* libctru gives the normal heap all remaining memory. On a New 3DS (124 MB
 * mode) that is ~115 MB, more than the 96 MB heap address range, so the
 * allocation failed and the app crashed at launch (svcBreak in
 * __system_allocateHeaps). Same logic as libctru, with the heap capped. */
#include <3ds/allocator/mappable.h>
#ifndef OS_HEAP_AREA_BEGIN
#define OS_HEAP_AREA_BEGIN 0x08000000
#define OS_HEAP_AREA_END   0x0E000000
#endif
#define GPSP3D_HEAP_CAP (80u << 20)
extern char *fake_heap_start, *fake_heap_end;
extern u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap;
void __system_allocateHeaps(void)
{
   Handle reslimit = 0;
   ResourceLimitType type = RESLIMIT_COMMIT;
   s64 max_commit = 0, cur_commit = 0;
   u32 remaining;

   if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE)))
      svcBreak(USERBREAK_PANIC);
   svcGetResourceLimitLimitValues(&max_commit, reslimit, &type, 1);
   svcGetResourceLimitCurrentValues(&cur_commit, reslimit, &type, 1);
   svcCloseHandle(reslimit);
   remaining = (u32)(max_commit - cur_commit) & ~0xFFF;

   if (__ctru_linear_heap_size + (1u << 20) > remaining)
      svcBreak(USERBREAK_PANIC);
   __ctru_heap_size = remaining - __ctru_linear_heap_size;
   if (__ctru_heap_size > GPSP3D_HEAP_CAP)
      __ctru_heap_size = GPSP3D_HEAP_CAP;
   if (__ctru_heap_size > OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN)
      __ctru_heap_size = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;

   if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size,
                                 MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE)))
      svcBreak(USERBREAK_PANIC);
   if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size,
                                 MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE)))
      svcBreak(USERBREAK_PANIC);
   mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
   fake_heap_start = (char*)__ctru_heap;
   fake_heap_end   = fake_heap_start + __ctru_heap_size;
}

/* From ctr_svchax.c (RetroArch): gives the process the kernel access the
 * dynarec needs (mapping its code cache executable, flushing caches). */
extern u32 __ctr_svchax;
Result svchax_init(bool patch_srv);
/* ctr_svchax.c's memchunkhax2 path (old firmware without Luma) needs the heap
 * start; libctru always places the application heap here. */
u32 __heapBase = 0x08000000;

/* gpSP's threaded renderer (gpsp/video_thread.h): draws scanlines on core 2 */
extern volatile int video_thread_enabled;
int      video_thread_start(int core);
void     video_thread_stop(void);
unsigned video_thread_last_render_us(void);
unsigned video_thread_last_hook_us(void);
void     video_thread_set_frame_hook(void (*hook)(const unsigned short *left, const unsigned short *right, unsigned pitch, void *arg), void *arg);
void     video_thread_set_stereo(const int depth[4]);
void     video_thread_set_stereo2(const int bg_depth[4], const int obj_depth[4]);
void     video_thread_set_tilt(int tilt16);
int      video_thread_redraw(void);
void     video_thread_set_gap_fill(int on);
void     video_thread_set_smart_layers(int on);
void     video_thread_set_fast3d(int on);
void     video_thread_set_color_correction(const unsigned short *lut);
unsigned video_thread_last_wait_us(void);
void     video_thread_set_low_priority(int low);
void     video_thread_defer_next_finish(void);
int      video_thread_complete_deferred(void);
extern int gpsp_frontend_skip;
void     video_thread_counters(unsigned *copies, unsigned *syncs);

#define APP_DIR      "sdmc:/3ds/gpsp3d"
#define CONFIG_PATH  APP_DIR "/gpsp3d.cfg"
#define LAUNCH_PATH  APP_DIR "/launch.txt"   /* written by HOME Menu forwarders */
/* RetroArch 3DS keeps game saves here (DEFAULT_DIR_SRAM = cores/savefiles) */
#define RA_SAVE_DIR  "sdmc:/retroarch/cores/savefiles"
#define RA_SAVE_DIR2 "sdmc:/retroarch/saves"

#define GBA_W 240
#define GBA_H 160
#define TOP_W 400
#define TOP_H 240
#define TOP_X0 ((TOP_W - GBA_W) / 2)
#define TOP_Y0 ((TOP_H - GBA_H) / 2)

#define TICKS_PER_MS ((double)SYSCLOCK_ARM11 / 1000.0)

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */


static char cur_dir[512]   = "sdmc:/";
static char rom_path[512];
static char rom_name[256];
static char save_path[512];
static char state_path[512];
static char save_dir[512];
static char state_dir[512];
static char old_save_dir[512];  /* where saves went before save_dir/save_ext were set */

static bool game_loaded;
static bool quit_app;
static bool frame_ready;

static u8    *state_buf;
static size_t state_size;
static u32    sram_hash;
static size_t sram_loaded;      /* size of the save file that was loaded (0 for .srm) */

/* settings (saved in gpsp3d.cfg) */
static int opt_frameskip = 1; /* 0 = off, 1 = auto (skip drawing a frame core 2 can't finish in time) */
/* Fast forward (Y): 0 hold, 1 toggle; speed 0 Max, 1 2x, 2 3x, 3 4x */
static int opt_ff_toggle = 0;
static int opt_ff_speed = 0;
static bool ff_on;               /* toggle mode: fast forward is on */
static int opt_rtc;           /* real-time clock: 0 Auto (the core decides), 1 On, 2 Off */
static int opt_intro;         /* 1: start games with the GBA intro (needs the user's gba_bios.bin) */
static int opt_bottom;        /* bottom screen while playing: 0 always on, 1 plain, 2 off (after 10 s) */
static int opt_slot;          /* save-state slot 0..9 */
static int opt_smooth = 1;    /* scaling (GPU display): 0 Sharp, 1 Crisp, 2 Smooth */
static int opt_crt;           /* screen effect (GPU display): 0 Off, 1 Scanlines, 2 LCD grid */
static int opt_audio_stretch = 1; /* slow the sound down instead of crackling */
static int opt_gapfill = 1;   /* 3D: fill seams between layers */
static int opt_lowprio;       /* core-2 render thread below system services (slower: off) */
static int opt_profiler = 1;
static int opt_colorcorr;     /* colour style: 0 Original, 1 GBA soft, 2 GBA screen */
static int opt_sat = 100;     /* saturation % */
static int opt_bright = 100;  /* brightness % (gamma) */
static int opt_core2 = 1;   /* draw on the second CPU core */
static int opt_display;     /* forward: defined with the display code */
/* where saves and states go (empty: RetroArch's save folder or next to the
 * ROM) and the saves' extension (empty: srm) */
static char opt_save_dir[256];
static char opt_state_dir[256];
static char opt_save_ext[8];
/* 3D settings (all games, or one game when it has its own profile) */
typedef struct {
   int on;          /* per-layer 3D */
   int pct;         /* strength 0..200 % */
   int style;       /* layer preset, see d3_styles */
   int layer[4];    /* depth of GBA priority 0..3 (px at 100%, + = nearer) */
   int sprite;      /* sprites this much nearer than BGs of the same priority */
   int focus;       /* moves every layer nearer (+) or further (-) */
   int smart;       /* layers that scroll together share one depth */
   int fast;        /* 3D method: 0 Accurate (draw each eye), 1 Fast (draw once, move pixels) */
   int tilt;        /* floor tilt: bottom of the screen nearer, top further (racing games) */
} s3d;
#define D3_STYLES 6
#define D3_FLATMAP 4
static const char *const d3_style_names[D3_STYLES] = { "Classic", "Pop-out", "Deep", "Gentle", "Flat map", "Custom" };
static const int d3_styles[D3_STYLES - 1][5] = {   /* front, middle, back, far, sprite pop */
   { 2, 0, -2, -4, 0 },   /* Classic: HUD in front, world behind the screen */
   { 4, 2, 0, -2, 0 },    /* Pop-out: everything comes out of the screen */
   { 0, -2, -4, -6, 0 },  /* Deep: HUD at the screen, world far behind */
   { 1, 0, -1, -2, 0 },   /* Gentle: subtle */
   { 3, 0, 0, 0, 2 },     /* Flat map: text/HUD in front, the map's layers at one
                           * depth, characters pop out (top-down games that build
                           * the map from several layers, e.g. Pokemon) */
};
static void d3_set_style(int style);
static s3d d3 = { 1, 100, 0, { 2, 0, -2, -4 }, 0, 0, 1, 0 };
static s3d d3_global;          /* all-games settings while a game uses its own */
static bool d3_pergame;        /* the loaded game has its own 3D profile */
static int opt_gpu = 1;     /* show frames with the GPU (needs core 2) */
static int opt_gpu_test;    /* diagnostics: 0 Full, 1 No draw, 2 No upload */
static bool stereo_mode;    /* top screen currently in 3D mode */
static bool wide_mode;      /* top screen in its 800px mode (2D at Fit / Stretch) */
static bool wide_allowed = true;   /* false on Old 2DS, which has no 800px mode */
static bool slider_flat;    /* 3D slider all the way down: show 2D */
/* bytes of one top-screen framebuffer (twice as many columns in 800px mode) */
#define TOP_FB_BYTES ((wide_mode ? 2u : 1u) * TOP_W * TOP_H * 2)

static bool vars_changed = true;

static void present_idle(void);   /* GPU hand-off thread is idle */

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static bool dir_exists(const char *p)
{
   struct stat st;
   return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool file_exists(const char *p)
{
   struct stat st;
   return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

/* mkdir -p */
static void mkdir_path(const char *p)
{
   char tmp[512];
   char *s;
   snprintf(tmp, sizeof(tmp), "%s", p);
   for (s = strchr(tmp, '/'); s; s = strchr(s + 1, '/'))
   {
      if (s == tmp || s[-1] == ':')
         continue;
      *s = '\0';
      mkdir(tmp, 0777);
      *s = '/';
   }
   mkdir(tmp, 0777);
}

static u32 hash_mem(const void *p, size_t n)
{
   const u32 *w = (const u32*)p;
   u32 h = 2166136261u;
   size_t i;
   for (i = 0; i < n / 4; i++)
      h = (h ^ w[i]) * 16777619u;
   return h;
}


/* ------------------------------------------------------------------ */
/* Config                                                              */
/* ------------------------------------------------------------------ */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a folder setting, without the trailing slash */
static void set_dir(char *out, size_t n, const char *v)
{
   size_t l;
   snprintf(out, n, "%s", v);
   l = strlen(out);
   while (l && out[l - 1] == '/')
      out[--l] = '\0';
}

/* the save extension: letters and digits, a leading dot is dropped */
static void set_ext(const char *v)
{
   size_t i;
   if (*v == '.')
      v++;
   for (i = 0; v[i]; i++)
      if (i >= sizeof(opt_save_ext) - 1 ||
          !((v[i] >= 'a' && v[i] <= 'z') || (v[i] >= 'A' && v[i] <= 'Z') || (v[i] >= '0' && v[i] <= '9')))
      {
         opt_save_ext[0] = '\0';
         return;
      }
   snprintf(opt_save_ext, sizeof(opt_save_ext), "%s", v);
}

/* 3D keys, shared by gpsp3d.cfg and per-game profiles */
static bool d3_parse(const char *k, const char *v, s3d *d)
{
   int x = atoi(v);
   if (!strcmp(k, "3d"))               d->on = x ? 1 : 0;
   else if (!strcmp(k, "3d_pct"))      d->pct = clampi(x, 0, 200);
   else if (!strcmp(k, "3d_style"))    d->style = x == 4 ? D3_STYLES - 1 : clampi(x, 0, 3);   /* old list */
   else if (!strcmp(k, "3d_preset"))   d->style = clampi(x, 0, D3_STYLES - 1);
   else if (!strcmp(k, "3d_front"))    d->layer[0] = clampi(x, -8, 8);
   else if (!strcmp(k, "3d_middle"))   d->layer[1] = clampi(x, -8, 8);
   else if (!strcmp(k, "3d_back"))     d->layer[2] = clampi(x, -8, 8);
   else if (!strcmp(k, "3d_far"))      d->layer[3] = clampi(x, -8, 8);
   else if (!strcmp(k, "3d_sprites"))  d->sprite = clampi(x, -4, 4);
   else if (!strcmp(k, "3d_focus"))    d->focus = clampi(x, -6, 6);
   else if (!strcmp(k, "3d_smart"))    d->smart = x ? 1 : 0;
   else if (!strcmp(k, "3d_method"))   d->fast = x ? 1 : 0;
   else if (!strcmp(k, "3d_tilt"))     d->tilt = clampi(x, -8, 8);
   else
      return false;
   return true;
}

static void d3_write(FILE *f, const s3d *d)
{
   fprintf(f, "3d=%d\n3d_pct=%d\n3d_preset=%d\n3d_front=%d\n3d_middle=%d\n3d_back=%d\n3d_far=%d\n"
           "3d_sprites=%d\n3d_focus=%d\n3d_smart=%d\n3d_method=%d\n3d_tilt=%d\n", d->on, d->pct, d->style, d->layer[0], d->layer[1],
           d->layer[2], d->layer[3], d->sprite, d->focus, d->smart, d->fast, d->tilt);
}

static void config_load(void)
{
   char line[600];
   FILE *f = fopen(CONFIG_PATH, "r");
   if (!f)
      return;
   while (fgets(line, sizeof(line), f))
   {
      char *eq = strchr(line, '=');
      char *nl = strpbrk(line, "\r\n");
      if (nl)
         *nl = '\0';
      if (!eq)
         continue;
      *eq++ = '\0';
      if (!strcmp(line, "last_dir") && dir_exists(eq))
         snprintf(cur_dir, sizeof(cur_dir), "%s", eq);
      else if (!strcmp(line, "save_dir"))
         set_dir(opt_save_dir, sizeof(opt_save_dir), eq);
      else if (!strcmp(line, "state_dir"))
         set_dir(opt_state_dir, sizeof(opt_state_dir), eq);
      else if (!strcmp(line, "save_ext"))
         set_ext(eq);
      else if (!strcmp(line, "frame_skip"))
         opt_frameskip = atoi(eq) ? 1 : 0;
      else if (!strcmp(line, "ff_toggle"))
         opt_ff_toggle = atoi(eq) ? 1 : 0;
      else if (!strcmp(line, "ff_speed"))
         opt_ff_speed = clampi(atoi(eq), 0, 3);
      else if (!strcmp(line, "rtc"))
         opt_rtc = clampi(atoi(eq), 0, 2);
      else if (!strcmp(line, "startup_intro"))
         opt_intro = atoi(eq) ? 1 : 0;
      else if (!strcmp(line, "bottom_screen"))
         opt_bottom = clampi(atoi(eq), 0, 2);
      else if (!strcmp(line, "profiler"))
         opt_profiler = atoi(eq) ? 1 : 0;
      else if (!strcmp(line, "saturation"))
      {
         opt_sat = atoi(eq);
         if (opt_sat < 50 || opt_sat > 150) opt_sat = 100;
      }
      else if (!strcmp(line, "brightness"))
      {
         opt_bright = atoi(eq);
         if (opt_bright < 70 || opt_bright > 130) opt_bright = 100;
      }
      else if (!strcmp(line, "color_style"))
      {
         opt_colorcorr = atoi(eq);
         if (opt_colorcorr < 0 || opt_colorcorr > 2)
            opt_colorcorr = 0;
      }
      else if (!strcmp(line, "core2"))
         opt_core2 = atoi(eq) ? 1 : 0;
      else if (!strcmp(line, "gpu"))
         opt_gpu = atoi(eq) ? 1 : 0;
      else if (!strcmp(line, "slot"))
         opt_slot = atoi(eq) >= 0 && atoi(eq) <= 9 ? atoi(eq) : 0;
      else if (!strcmp(line, "scaling"))
         opt_smooth = clampi(atoi(eq), 0, 2);
      else if (!strcmp(line, "screen_effect"))
         opt_crt = clampi(atoi(eq), 0, 2);
      else if (!strcmp(line, "audio_stretch"))
         opt_audio_stretch = atoi(eq) ? 1 : 0;
      else if (!strcmp(line, "3d_gap_fill"))
         opt_gapfill = atoi(eq) ? 1 : 0;
      else if (!strcmp(line, "core2_low_prio"))
         opt_lowprio = atoi(eq) ? 1 : 0;
      else if (!strcmp(line, "gpu_test"))
      {
         opt_gpu_test = 0;   /* diagnostics only: never carried over */
      }
      else if (!strcmp(line, "3d_strength"))
         d3.pct = atoi(eq) ? 200 : 100;   /* old Normal/Strong */
      else if (d3_parse(line, eq, &d3))
         ;
      else if (!strcmp(line, "display"))
      {
         opt_display = atoi(eq);
         if (opt_display < 0 || opt_display > 2)
            opt_display = 1;
      }
   }
   fclose(f);
}

static void config_save(void)
{
   FILE *f;
   mkdir("sdmc:/3ds", 0777);
   mkdir(APP_DIR, 0777);
   f = fopen(CONFIG_PATH, "w");
   if (!f)
      return;
   fprintf(f, "last_dir=%s\nframe_skip=%d\nprofiler=%d\ncolor_style=%d\ncore2=%d\ndisplay=%d\ngpu=%d\n"
         "slot=%d\nscaling=%d\naudio_stretch=%d\ncore2_low_prio=%d\nsaturation=%d\nbrightness=%d\n3d_gap_fill=%d\nscreen_effect=%d\n"
         "ff_toggle=%d\nff_speed=%d\nrtc=%d\nstartup_intro=%d\nbottom_screen=%d\n",
         cur_dir, opt_frameskip, opt_profiler, opt_colorcorr, opt_core2, opt_display, opt_gpu,
         opt_slot, opt_smooth, opt_audio_stretch, opt_lowprio, opt_sat, opt_bright, opt_gapfill, opt_crt,
         opt_ff_toggle, opt_ff_speed, opt_rtc, opt_intro, opt_bottom);
   /* no menu entries for these: written even when empty so they can be found */
   fprintf(f, "save_dir=%s\nstate_dir=%s\nsave_ext=%s\n", opt_save_dir, opt_state_dir, opt_save_ext);
   /* the all-games 3D settings (a game with its own profile keeps them aside) */
   d3_write(f, d3_pergame ? &d3_global : &d3);
   fclose(f);
}

/* GPU freeze guard: before each risky GPU step its name is written to a
 * file, and the file is removed once the step is known to work. If the
 * 3DS froze, the file is still there on the next launch: the GPU display is
 * then turned off and the step is shown, so nobody has to hard-reset twice. */
#define GPU_GUARD APP_DIR "/gpu_check.txt"
static bool gpu_guarded;
static int gpu_guard_frames;
static void gpu_guard_set(const char *stage)
{
   gpu_guard_frames = 0;
   FILE *f = fopen(GPU_GUARD, "w");
   if (f)
   {
      fputs(stage, f);
      fclose(f);
      gpu_guarded = true;
   }
}
static void gpu_guard_clear(void)
{
   if (gpu_guarded)
   {
      remove(GPU_GUARD);
      gpu_guarded = false;
   }
}
/* during the first 3 GPU frames, record each step (frame number + step) */
static void gpu_guard_step(const char *step)
{
   char buf[64];
   FILE *f;
   if (!gpu_guarded || gpu_guard_frames > 3)
      return;
   snprintf(buf, sizeof(buf), "GPU frame %d: %s", gpu_guard_frames, step);
   f = fopen(GPU_GUARD, "w");
   if (f)
   {
      fputs(buf, f);
      fclose(f);
   }
}
/* returns the step that froze last time, or NULL */
static const char *gpu_guard_check(void)
{
   static char stage[64];
   FILE *f = fopen(GPU_GUARD, "r");
   size_t n;
   if (!f)
      return NULL;
   n = fread(stage, 1, sizeof(stage) - 1, f);
   fclose(f);
   stage[n] = '\0';
   remove(GPU_GUARD);
   return stage;
}

/* ------------------------------------------------------------------ */
/* Video: GBA frame -> top framebuffer (RGB565, rotated, 1:1 centred)  */
/* ------------------------------------------------------------------ */

static u64 t_blit_acc;

/* Cache flushes. The libctru helpers (gfxFlushBuffers, DSP_FlushDataCache)
 * ask a system service to do it, and on an Old 3DS those services share core 2
 * with our renderer, so each request waits its turn (several ms per frame).
 * With kernel access (svchax) the same flush is a direct kernel call. */
static bool fast_flush;

static void flush_gpu(const void *p, u32 size)
{
   if (fast_flush)
      svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)p, size);
   else
      GSPGPU_FlushDataCache(p, size);
}

static void flush_dsp(const void *p, u32 size)
{
   if (fast_flush)
      svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)p, size);
   else
      DSP_FlushDataCache(p, size);
}

static bool gpu_ok;
static int opt_gpu_fwd(void);
#define GPU_ACTIVE (gpu_ok && opt_gpu_fwd())
static void gpu_clear(void);
static void clear_top(void)
{
   int i;
   if (GPU_ACTIVE)
   {
      /* citro3d owns the top screen: clear it with the GPU (the render
       * thread is idle whenever this is called) */
      gpu_clear();
      return;
   }
   for (i = 0; i < 2; i++)
   {
      u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
      memset(fb, 0, TOP_FB_BYTES);
      if (stereo_mode)
         memset(gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), 0, TOP_FB_BYTES);
      flush_gpu(fb, TOP_FB_BYTES);
      if (stereo_mode)
         flush_gpu(gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), TOP_FB_BYTES);
      gfxScreenSwapBuffers(GFX_TOP, stereo_mode);
      gspWaitForVBlank();
   }
}

/* Screen size: 0 = Original (1:1), 1 = Fit (1.5x, 360x240), 2 = Stretch (400x240) */
static int opt_display = 1;   /* (tentative definition above) */
static const char *display_names[] = { "Original", "Fit", "Stretch" };

extern const u16 gba_cc_lut[];   /* gpSP's GBA screen colour correction table */
static u16 *cc_lut;               /* colour table for the current settings */
static bool cc_active;
static const char *cc_names[] = { "Original", "GBA soft", "GBA screen" };

/* Colour table for the current setting (indexed by RGB555), or NULL */
static const u16 *cc_table(void)
{
   return cc_active ? cc_lut : NULL;
}

/* Rebuild the table from Color style + Saturation + Brightness. */
static void cc_rebuild(void)
{
   unsigned i;
   u8 gam[256];
   float sat = opt_sat / 100.0f;
   cc_active = opt_colorcorr || opt_sat != 100 || opt_bright != 100;
   if (!cc_active)
      return;
   if (!cc_lut)
      cc_lut = (u16*)malloc(32768 * sizeof(u16));
   if (!cc_lut)
   {
      cc_active = false;
      return;
   }
   /* brightness as a gamma curve: lifts or lowers mid-tones, keeps black
    * black and white white */
   {
      float g = 100.0f / (float)opt_bright;
      for (i = 0; i < 256; i++)
      {
         float v = i / 255.0f;
         gam[i] = (u8)(powf(v, g * g) * 255.0f + 0.5f);
      }
   }
   for (i = 0; i < 32768; i++)
   {
      float r = ((i >> 10) & 31) * (255.0f / 31.0f);
      float g = ((i >> 5) & 31) * (255.0f / 31.0f);
      float b = (i & 31) * (255.0f / 31.0f);
      float l;
      int ri, gi, bi;
      if (opt_colorcorr)
      {
         u16 c = gba_cc_lut[i];
         float cr = (c >> 11) * (255.0f / 31.0f), cg = ((c >> 5) & 63) * (255.0f / 63.0f), cb = (c & 31) * (255.0f / 31.0f);
         if (opt_colorcorr == 1)
         {
            r = (r + cr) * 0.5f; g = (g + cg) * 0.5f; b = (b + cb) * 0.5f;
         }
         else
         {
            r = cr; g = cg; b = cb;
         }
      }
      l = 0.299f * r + 0.587f * g + 0.114f * b;
      r = l + (r - l) * sat;
      g = l + (g - l) * sat;
      b = l + (b - l) * sat;
      ri = r < 0 ? 0 : r > 255 ? 255 : (int)(r + 0.5f);
      gi = g < 0 ? 0 : g > 255 ? 255 : (int)(g + 0.5f);
      bi = b < 0 ? 0 : b > 255 ? 255 : (int)(b + 0.5f);
      ri = gam[ri]; gi = gam[gi]; bi = gam[bi];
      cc_lut[i] = (u16)(((ri >> 3) << 11) | ((gi >> 2) << 5) | (bi >> 3));
   }
}

static inline u16 cc_px(const u16 *lut, u16 c) { return lut[((c & 0xFFC0) >> 1) | (c & 0x1F)]; }

/* Copies a GBA frame (RGB565, 'pitch' pixels per row) to the top screen's back
 * buffer in the selected size. The 3DS framebuffer is stored rotated
 * (column-major, 240 pixels per column, bottom of the screen first), so each
 * GBA row has to be scattered across many screen columns. Doing that one
 * column at a time misses the cache on nearly every source pixel; instead the
 * picture is copied in strips 16 GBA pixels wide: each source row segment is
 * one cache line, and the 16..32 destination columns it feeds stay in cache.
 * Scaling repeats whole rows (160 -> 240: every odd GBA row twice) and whole
 * columns (240 -> 360 for Fit, 400 for Stretch). */
#define STRIP 16

static void blit_to_top(const u16 *src, unsigned pitch, gfx3dSide_t side)
{
   u16 *fb = (u16*)gfxGetFramebuffer(GFX_TOP, side, NULL, NULL);
   const u16 *lut = video_thread_enabled ? NULL : cc_table();   /* core 2 corrects the palette */
   const bool cc = lut != NULL;
   const bool scaled = opt_display != 0;
   /* in 800px mode every column is half as wide: twice as many of them
    * (Fit: exactly 3 per GBA pixel) */
   const unsigned FW = wide_mode ? TOP_W * 2 : TOP_W;
   const unsigned W  = (opt_display == 0 ? GBA_W : opt_display == 1 ? 360 : 400) * (wide_mode ? 2 : 1);
   const unsigned x0 = (FW - W) / 2;
   unsigned xs, k, y, j;

   for (xs = 0; xs < GBA_W; xs += STRIP)
   {
      u16 *col[STRIP];      /* write pointer of each GBA column's first screen column */
      u8   reps[STRIP];     /* screen columns per GBA column (1 to 4) */
      for (k = 0; k < STRIP; k++)
      {
         unsigned x = xs + k, c0 = W * x / GBA_W, c1 = W * (x + 1) / GBA_W;
         col[k]  = fb + (x0 + c0) * TOP_H + (scaled ? TOP_H - 1 : TOP_H - 1 - TOP_Y0);
         reps[k] = (u8)(c1 - c0);
      }

      if (!scaled)
      {
         for (y = 0; y < GBA_H; y++)
         {
            const u16 *s = src + y * pitch + xs;
            for (k = 0; k < STRIP; k++)
            {
               u16 c = s[k];
               if (cc) c = cc_px(lut, c);
               *col[k]-- = c;
            }
         }
         continue;
      }

      for (y = 0; y < GBA_H; y += 2)
      {
         const u16 *s0 = src + y * pitch + xs;
         const u16 *s1 = s0 + pitch;
         for (k = 0; k < STRIP; k++)
         {
            u16 a = s0[k], b = s1[k];
            u16 *d = col[k];
            if (cc) { a = cc_px(lut, a); b = cc_px(lut, b); }
            for (j = 0; j < reps[k]; j++, d += TOP_H)
            {
               d[0] = a;       /* even GBA row: one screen row  */
               d[-1] = b;      /* odd GBA row: two screen rows  */
               d[-2] = b;
            }
            col[k] -= 3;
         }
      }
   }
}

/* ------------------------------------------------------------------ */
/* GPU presentation (used from core 2): the finished GBA frame goes to a */
/* texture through the GPU's transfer engine and is drawn as a scaled    */
/* rectangle, once per eye in 3D. The CPU only does one sequential copy  */
/* into a GPU-visible buffer. Uses mGBA's ctr-gpu helper and shader.     */
/* ------------------------------------------------------------------ */

#define TEX_FLAGS (GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) | \
                   GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) | \
                   GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
#define FB_FLAGS  (GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565))   /* RGB8 in -> 16-bit out hangs the transfer engine */

static C3D_RenderTarget *gpu_top[2];   /* left, right eye */
static C3D_Tex           gpu_tex[2];
static u16              *gpu_stage[2]; /* 256 x 160, linear memory */

static int opt_smooth_fwd(void);
/* Crisp scaling ("sharp bilinear"): the frame is first enlarged to exactly
 * 2x with hard pixel edges, then that is scaled to the screen with linear
 * filtering. Every GBA pixel comes out the same size with only a hairline
 * of blending at its edges (plain nearest scaling to 1.5x makes some pixels
 * one screen pixel wide and others two). */
/* Screen effects: an 8x8 alpha pattern repeated once per GBA pixel and
 * drawn in black over the picture (rows: scanlines, rows + columns: LCD
 * grid). Texels are stored in the GPU's tiled (Morton) order. */
static C3D_Tex           gpu_fx_tex[2];
static bool              gpu_fx_ok;
static void gpu_fx_fill(C3D_Tex *t, bool cols)
{
   u8 *d = (u8*)t->data;
   int x, y;
   for (y = 0; y < 8; y++)
      for (x = 0; x < 8; x++)
      {
         int m = 0, b;
         int a = 0;
         for (b = 0; b < 3; b++)
            m |= ((x >> b) & 1) << (2 * b) | ((y >> b) & 1) << (2 * b + 1);
         if (y >= 6)
            a = cols ? 0x70 : 0x78;   /* gap between rows */
         if (cols && x >= 7)
            a = a ? 0x90 : 0x60;      /* gap between columns */
         d[m] = (u8)a;
      }
   C3D_TexFlush(t);
}

static C3D_Tex           gpu_up_tex[2];
static C3D_RenderTarget *gpu_up[2];
static bool              gpu_up_ok;
#define UP_MULT 2

/* 800px mode (2D): its own 800-wide screen target, and for Sharp a buffer
 * 3x as wide as the GBA screen (720 = Fit exactly: hard columns, no
 * blending across; rows are 2x, then 1.5x like Crisp). Made when first
 * needed. The buffer is square, like the 2x one, so the viewport (which
 * ctrSetViewportSize gives with width and height swapped) fits either way. */
static C3D_RenderTarget *gpu_wide;
static C3D_Tex           gpu_wup_tex;
static C3D_RenderTarget *gpu_wup;
static bool              gpu_wup_ok;
static bool              gpu_linked_wide;   /* gpu_wide is the top screen's output */
static bool              gpu_wide_failed;   /* no VRAM for it: 800px mode on the CPU path only */
#define WUP_MULT_X 3

static void gpu_apply_filter(void)
{
   int e;
   GPU_TEXTURE_FILTER_PARAM f;
   present_idle();
   f = opt_smooth_fwd() == 2 ? GPU_LINEAR : GPU_NEAREST;
   if (!gpu_ok)
      return;
   for (e = 0; e < 2; e++)
      C3D_TexSetFilter(&gpu_tex[e], f, f);
}

static bool gpu_init(void)
{
   int e;
   if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
      return false;
   for (e = 0; e < 2; e++)
   {
      gpu_top[e] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, -1);   /* -1: no depth buffer */
      if (!gpu_top[e])
         return false;
      C3D_RenderTargetSetOutput(gpu_top[e], GFX_TOP, e ? GFX_RIGHT : GFX_LEFT, FB_FLAGS);
      if (!C3D_TexInit(&gpu_tex[e], 256, 256, GPU_RGB565))
         return false;
      C3D_TexSetFilter(&gpu_tex[e], opt_smooth_fwd() == 2 ? GPU_LINEAR : GPU_NEAREST,
            opt_smooth_fwd() == 2 ? GPU_LINEAR : GPU_NEAREST);
      C3D_TexSetWrap(&gpu_tex[e], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
      memset(gpu_tex[e].data, 0, gpu_tex[e].size);
      gpu_stage[e] = (u16*)linearAlloc(256 * GBA_H * 2);
      if (!gpu_stage[e])
         return false;
      memset(gpu_stage[e], 0, 256 * GBA_H * 2);
   }
   /* 2x buffers for Crisp scaling (optional: without them Crisp = Sharp) */
   gpu_up_ok = true;
   for (e = 0; e < 2 && gpu_up_ok; e++)
   {
      if (!C3D_TexInitVRAM(&gpu_up_tex[e], 512, 512, GPU_RGB565))
      {
         gpu_up_ok = false;
         break;
      }
      gpu_up[e] = C3D_RenderTargetCreateFromTex(&gpu_up_tex[e], GPU_TEXFACE_2D, 0, -1);
      if (!gpu_up[e])
         gpu_up_ok = false;
      C3D_TexSetFilter(&gpu_up_tex[e], GPU_LINEAR, GPU_LINEAR);
      C3D_TexSetWrap(&gpu_up_tex[e], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
   }
   gpu_fx_ok = true;
   for (e = 0; e < 2; e++)
   {
      if (!C3D_TexInit(&gpu_fx_tex[e], 8, 8, GPU_A8))
      {
         gpu_fx_ok = false;
         break;
      }
      C3D_TexSetFilter(&gpu_fx_tex[e], GPU_LINEAR, GPU_LINEAR);
      C3D_TexSetWrap(&gpu_fx_tex[e], GPU_REPEAT, GPU_REPEAT);
      gpu_fx_fill(&gpu_fx_tex[e], e == 1);
   }
   if (!ctrInitGpu())
      return false;
   if (gpu_up_ok)
   {
      C3D_FrameBegin(0);
      for (e = 0; e < 2; e++)
      {
         C3D_FrameDrawOn(gpu_up[e]);
         C3D_RenderTargetClear(gpu_up[e], C3D_CLEAR_COLOR, 0, 0);
      }
      C3D_FrameEnd(0);
   }
   return true;
}

/* GPU frames never wait for the GPU: if it is still busy with the previous
 * frame, this one is skipped (counted in gpu_skips). So a stuck GPU shows a
 * frozen picture and a rising skip count instead of freezing the 3DS. */
static u32 gpu_skips, gpu_sent;
/* GPU "job finished" interrupts: memory fill, transfer, drawing */
static volatile u32 gpu_irq[3];
static void gpu_irq_cb(void *p) { (*(volatile u32*)p)++; }
static void gpu_unstick(s64 wait_ticks);
/* Hooks registered later run first, so this one (added after C3D_Init)
 * runs before citro3d's, which would wait forever on a stuck GPU queue and
 * leave HOME / Close hanging. */
static aptHookCookie gpu_apt_cookie;
static void gpu_apt_hook(APT_HookType hook, void *param)
{
   (void)param;
   if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP || hook == APTHOOK_ONEXIT)
   {
      present_idle();
      gpu_unstick(SYSCLOCK_ARM11 / 10);
   }
}
static void gpu_irq_hook(void)
{
   aptHook(&gpu_apt_cookie, gpu_apt_hook, NULL);
   gspSetEventCallback(GSPGPU_EVENT_PSC0, gpu_irq_cb, (void*)&gpu_irq[0], false);
   gspSetEventCallback(GSPGPU_EVENT_PPF,  gpu_irq_cb, (void*)&gpu_irq[1], false);
   gspSetEventCallback(GSPGPU_EVENT_P3D,  gpu_irq_cb, (void*)&gpu_irq[2], false);
}
static u32 *gpu_cmd_start;

/* libctru's GPU job queue moves on when a "job finished" interrupt arrives.
 * If one never arrives the queue stays busy forever, and citro3d then also
 * blocks the HOME button (it waits for the queue before suspending). This
 * pushes a stuck queue along by hand so the app and HOME keep working. */
extern void gxCmdQueueInterrupt(GSPGPU_Event irq);
static u32 gpu_rescues;
static int gpu_skip_run;
static void gpu_unstick(s64 wait_ticks)
{
   int i;
   if (gxCmdQueueWait(NULL, wait_ticks))
      return;
   for (i = 0; i < 32 && !gxCmdQueueWait(NULL, 0); i++)
      gxCmdQueueInterrupt(GSPGPU_EVENT_PPF);
   gpu_rescues++;
}

static bool gpu_frame_begin(void)
{
   u32 *buf, size, off;
   if (!C3D_FrameBegin(C3D_FRAME_NONBLOCK))
   {
      gpu_skips++;
      if (++gpu_skip_run >= 90)
      {
         gpu_skip_run = 0;
         gpu_unstick(0);
      }
      return false;
   }
   gpu_skip_run = 0;
   GPUCMD_GetBuffer(&buf, &size, &off);
   gpu_cmd_start = buf + off;
   return true;
}

static void gpu_frame_end(void)
{
   if (fast_flush)
   {
      /* flush only what the CPU wrote this frame: the command list (all of
       * it, including parts split off for texture transfers) and vertices */
      u32 *buf, size, off;
      GPUCMD_GetBuffer(&buf, &size, &off);
      if (buf + off > gpu_cmd_start)
         svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)gpu_cmd_start, (u32)((buf + off) - gpu_cmd_start) * 4);
      ctrFlushVertices();
      C3D_FrameEnd(GX_CMDLIST_FLUSH);
   }
   else
      C3D_FrameEnd(0);
}

/* the GPU side of the 800px mode; call with the GPU idle */
static bool gpu_wide_init(void)
{
   if (gpu_wide)
      return true;
   if (gpu_wide_failed)
      return false;
   gpu_wide = C3D_RenderTargetCreate(240, TOP_W * 2, GPU_RB_RGBA8, -1);
   if (!gpu_wide)
   {
      gpu_wide_failed = true;
      return false;
   }
   if (C3D_TexInitVRAM(&gpu_wup_tex, 1024, 1024, GPU_RGB565))
   {
      gpu_wup = C3D_RenderTargetCreateFromTex(&gpu_wup_tex, GPU_TEXFACE_2D, 0, -1);
      gpu_wup_ok = gpu_wup != NULL;
      C3D_TexSetFilter(&gpu_wup_tex, GPU_LINEAR, GPU_LINEAR);
      C3D_TexSetWrap(&gpu_wup_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
      if (gpu_wup_ok)
      {
         C3D_FrameBegin(0);
         C3D_FrameDrawOn(gpu_wup);
         C3D_RenderTargetClear(gpu_wup, C3D_CLEAR_COLOR, 0, 0);
         C3D_FrameEnd(0);
      }
   }
   return true;
}

/* which render target(s) the GPU sends to the top screen: one 800px wide,
 * or one per eye (400px); call with the GPU idle */
static void gpu_link(bool wide)
{
   if (wide == gpu_linked_wide)
      return;
   if (wide)
   {
      C3D_RenderTargetSetOutput(NULL, GFX_TOP, GFX_RIGHT, 0);
      C3D_RenderTargetSetOutput(gpu_wide, GFX_TOP, GFX_LEFT, FB_FLAGS);
   }
   else
   {
      C3D_RenderTargetSetOutput(gpu_top[0], GFX_TOP, GFX_LEFT, FB_FLAGS);
      C3D_RenderTargetSetOutput(gpu_top[1], GFX_TOP, GFX_RIGHT, FB_FLAGS);
   }
   gpu_linked_wide = wide;
}

static void gpu_clear(void)
{
   int e;
   present_idle();
   if (!gpu_frame_begin())
      return;
   ctrStartFrame();
   if (gpu_linked_wide)
   {
      C3D_FrameDrawOn(gpu_wide);
      C3D_RenderTargetClear(gpu_wide, C3D_CLEAR_COLOR, 0, 0);
   }
   else
   for (e = 0; e < (stereo_mode ? 2 : 1); e++)
   {
      C3D_FrameDrawOn(gpu_top[e]);
      C3D_RenderTargetClear(gpu_top[e], C3D_CLEAR_COLOR, 0, 0);   /* colour only: there is no depth buffer */
   }
   gpu_frame_end();
}

/* Step 1, on core 2: copy the finished frame(s) into GPU-visible memory.
 * Pure CPU work, no system calls (the GPU itself is driven from the main
 * thread: talking to the GPU from core 2 froze the 3DS). */
static int gpu_staged_uploads;
static void gpu_stage_frame(const u16 *left, const u16 *right, unsigned pitch)
{
   int uploads = (stereo_mode && right) ? 2 : 1, e;
   unsigned y;
   for (e = 0; e < uploads; e++)
   {
      const u16 *src = e ? right : left;
      for (y = 0; y < GBA_H; y++)
         memcpy(gpu_stage[e] + y * 256, src + y * pitch, GBA_W * 2);
      if (fast_flush)
         svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)gpu_stage[e], 256 * GBA_H * 2);
   }
   gpu_staged_uploads = uploads;
}

/* Step 2, on the main thread: hand the frame to the GPU. The GPU then copies
 * it into textures and draws it scaled (once per eye) while we emulate on. */
static void gpu_submit(void)
{
   static const s16 rect[3][4] = { { 80, 40, 240, 160 }, { 20, 0, 360, 240 }, { 0, 0, 400, 240 } };
   static const s16 rect_wide[3][4] = { { 160, 40, 480, 160 }, { 40, 0, 720, 240 }, { 0, 0, 800, 240 } };
   const bool wide = gpu_linked_wide;
   const s16 *r = wide ? rect_wide[opt_display] : rect[opt_display];
   const s16 screen_w = wide ? TOP_W * 2 : TOP_W;
   int uploads = gpu_staged_uploads, eyes = (stereo_mode && !wide) ? 2 : 1, e;
   /* Sharp and Crisp at Fit / Stretch: enlarge with hard pixels to a whole
    * multiple first, then scale that to the screen with linear filtering,
    * so every GBA pixel comes out the same size (plain nearest scaling to
    * 1.5x makes some one screen pixel wide and others two, which breaks
    * up dithered transparency). In 800px mode Sharp enlarges 3x across:
    * at Fit that is the screen's own width, so the columns stay hard. */
   bool crisp = gpu_up_ok && opt_smooth != 2 && opt_display != 0;
   bool sharp3 = crisp && wide && opt_smooth == 0 && gpu_wup_ok;

   if (gpu_guarded && ++gpu_guard_frames == 180)
      gpu_guard_clear();   /* 3 seconds of GPU frames: it works */

   if (!fast_flush)
      for (e = 0; e < uploads; e++)
         GSPGPU_FlushDataCache(gpu_stage[e], 256 * GBA_H * 2);

   gpu_guard_step("waiting for the GPU");
   if (!gpu_frame_begin())
      return;
   ctrStartFrame();
   /* frame -> texture (queued inside the frame: no waiting) */
   gpu_guard_step("texture upload");
   gpu_sent++;
   if (opt_gpu_test != 2)
   for (e = 0; e < uploads; e++)
      C3D_SyncDisplayTransfer((u32*)gpu_stage[e], GX_BUFFER_DIM(256, GBA_H),
            (u32*)gpu_tex[e].data, GX_BUFFER_DIM(256, 256), TEX_FLAGS);
   gpu_guard_step("drawing");
   for (e = 0; e < eyes; e++)
   {
      C3D_RenderTarget *screen = wide ? gpu_wide : gpu_top[e];
      C3D_FrameDrawOn(screen);
      C3D_RenderTargetClear(screen, C3D_CLEAR_COLOR, 0, 0);   /* colour only: there is no depth buffer */
      if (opt_gpu_test == 1)
         continue;
      if (sharp3)
      {
         /* 1: frame -> 3x across, 2x down, hard pixels */
         C3D_FrameDrawOn(gpu_wup);
         ctrSetViewportSize(1024, 1024, false);
         ctrActivateTexture(&gpu_tex[0]);
         ctrAddRectEx(0xFFFFFFFF, 0, 0, GBA_W * WUP_MULT_X, GBA_H * UP_MULT, 0, 0, GBA_W, GBA_H, 0);
         ctrFlushBatch();
         /* 2: -> screen, linear (at Fit: 1:1 across, 1.5x down) */
         C3D_FrameDrawOn(screen);
         ctrSetViewportSize(screen_w, TOP_H, true);
         ctrActivateTexture(&gpu_wup_tex);
         ctrAddRectEx(0xFFFFFFFF, r[0], r[1], r[2], r[3], 0, 0, GBA_W * WUP_MULT_X, GBA_H * UP_MULT, 0);
         ctrFlushBatch();
      }
      else if (crisp)
      {
         /* 1: frame -> 2x buffer, hard pixels */
         C3D_FrameDrawOn(gpu_up[e]);
         ctrSetViewportSize(256 * UP_MULT, 256 * UP_MULT, false);
         ctrActivateTexture(&gpu_tex[e < uploads ? e : 0]);
         ctrAddRectEx(0xFFFFFFFF, 0, 0, GBA_W * UP_MULT, GBA_H * UP_MULT, 0, 0, GBA_W, GBA_H, 0);
         ctrFlushBatch();
         /* 2: 2x buffer -> screen, linear */
         C3D_FrameDrawOn(screen);
         ctrSetViewportSize(screen_w, TOP_H, true);
         ctrActivateTexture(&gpu_up_tex[e]);
         ctrAddRectEx(0xFFFFFFFF, r[0], r[1], r[2], r[3], 0, 0, GBA_W * UP_MULT, GBA_H * UP_MULT, 0);
         ctrFlushBatch();
      }
      else
      {
         ctrSetViewportSize(screen_w, TOP_H, true);
         ctrActivateTexture(&gpu_tex[e < uploads ? e : 0]);
         ctrAddRectEx(0xFFFFFFFF, r[0], r[1], r[2], r[3], 0, 0, GBA_W, GBA_H, 0);
         ctrFlushBatch();
      }
      if (opt_crt && gpu_fx_ok)
      {
         /* black, with the pattern's alpha: 8 texels per GBA pixel */
         ctrActivateTexture(&gpu_fx_tex[opt_crt - 1]);
         ctrAddRectEx(0xFF000000, r[0], r[1], r[2], r[3], 0, 0, GBA_W * 8, GBA_H * 8, 0);
         ctrFlushBatch();
      }
   }
   ctrEndFrame();
   gpu_guard_step("sending the frame");
   gpu_frame_end();
   gpu_guard_step("after sending the frame");
}

/* GPU hand-off thread (main CPU core, just above the emulation thread).
 * Sending a frame to the GPU means calls into the system's GPU service,
 * which runs on core 2 and can keep the caller waiting for milliseconds
 * while our renderer is busy there. This thread does that waiting, so the
 * emulation carries on meanwhile. */
static Thread         present_thr;
static LightEvent     present_ev;
static volatile bool  present_busy, present_quit;
static volatile u32   present_us_acc;

static void present_main(void *arg)
{
   (void)arg;
   for (;;)
   {
      u64 t0;
      LightEvent_Wait(&present_ev);
      if (present_quit)
         break;
      t0 = svcGetSystemTick();
      gpu_submit();
      present_us_acc += (u32)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000000));
      __dmb();
      present_busy = false;
   }
}

static void present_start(void)
{
   s32 prio = 0x30;
   if (present_thr)
      return;
   svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
   LightEvent_Init(&present_ev, RESET_ONESHOT);
   present_quit = false;
   present_busy = false;
   present_thr = threadCreate(present_main, NULL, 32 * 1024, prio > 0x18 ? prio - 1 : prio, -2, false);
}

/* wait until the hand-off thread is idle (before using the GPU directly) */
static void present_idle(void)
{
   while (present_busy)
      svcSleepThread(200000);
}

static void present_stop(void)
{
   if (!present_thr)
      return;
   present_idle();
   present_quit = true;
   LightEvent_Signal(&present_ev);
   threadJoin(present_thr, U64_MAX);
   threadFree(present_thr);
   present_thr = NULL;
}

static void gpu_request_present(void)
{
   if (!present_thr)
   {
      u64 t0 = svcGetSystemTick();
      gpu_submit();
      present_us_acc += (u32)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000000));
      return;
   }
   if (present_busy)
   {
      gpu_skips++;   /* previous frame still being handed over: drop this one */
      return;
   }
   present_busy = true;
   __dmb();
   LightEvent_Signal(&present_ev);
}

/* Runs on core 2 when the threaded renderer finishes a frame. */
static volatile bool hook_blitted;
static volatile bool hook_presented;   /* GPU path: the frame is already on its way to the screen */
static volatile u32 hook_copy_us, hook_flush_us;
static void frame_hook(const unsigned short *left, const unsigned short *right, unsigned pitch, void *arg)
{
   u64 t0 = svcGetSystemTick(), t1;
   (void)arg;
   if (GPU_ACTIVE)
   {
      gpu_stage_frame(left, right, pitch);
      hook_copy_us = (u32)((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000000));
      hook_flush_us = 0;
      hook_presented = true;
      return;
   }
   blit_to_top(left, pitch, GFX_LEFT);
   if (stereo_mode)
      blit_to_top(right ? right : left, pitch, GFX_RIGHT);
   t1 = svcGetSystemTick();
   hook_copy_us = (u32)((t1 - t0) / (SYSCLOCK_ARM11 / 1000000));
   /* clean this core's cache for the buffers; the main core can't do it for us */
   if (fast_flush)
   {
      svcFlushProcessDataCache(CUR_PROCESS_HANDLE,
            (u32)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), TOP_FB_BYTES);
      if (stereo_mode)
         svcFlushProcessDataCache(CUR_PROCESS_HANDLE,
               (u32)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), TOP_FB_BYTES);
   }
   hook_flush_us = (u32)((svcGetSystemTick() - t1) / (SYSCLOCK_ARM11 / 1000000));
   hook_blitted = true;
}

/* Puts the top screen in or out of 3D mode to match the settings. 3D needs
 * the renderer on core 2 (each line is drawn once per eye there). */
/* 3D slider all the way down = 2D (a little hysteresis so it can't flap) */
static void slider_flat_update(void)
{
   float sl = osGet3DSliderState();
   if (slider_flat && sl > 0.02f)
      slider_flat = false;
   else if (!slider_flat && sl < 0.005f)
      slider_flat = true;
}

/* the top screen mode the settings and the 3D slider ask for */
static void mode_wanted(bool *want3d, bool *wide)
{
   slider_flat_update();
   *want3d = d3.on && video_thread_enabled && !slider_flat;
   /* 2D at Fit / Stretch: the 800px mode, for exact 3x columns at Fit */
   *wide = !*want3d && wide_allowed && opt_display != 0;
}

static void stereo_apply(void)
{
   bool want, wide;
   if (video_thread_enabled)
      video_thread_set_color_correction(cc_table());
   mode_wanted(&want, &wide);
   if (wide && GPU_ACTIVE)
   {
      present_idle();
      if (!gpu_wide_init())
         wide = false;
   }
   if (want != stereo_mode || wide != wide_mode)
   {
      present_idle();
      stereo_mode = want;
      wide_mode = wide;
      if (want)
         gfxSet3D(true);
      else
         gfxSetWide(wide);   /* false: plain 2D */
   }
   if (gpu_ok && gpu_linked_wide != (wide_mode && GPU_ACTIVE))
   {
      present_idle();
      gpu_unstick(SYSCLOCK_ARM11 / 10);
      gpu_link(wide_mode && GPU_ACTIVE);
   }
   if (!want && video_thread_enabled)
      video_thread_set_stereo(NULL);
}

/* during play: the 3D slider went to or left the bottom (or the settings
 * changed): switch the top screen between 3D and 2D/800px. Only call when
 * core 2 is not drawing. Returns true if it switched. */
static bool stereo_mode_check(void)
{
   bool want, wide;
   mode_wanted(&want, &wide);
   if (wide && GPU_ACTIVE && gpu_wide_failed)
      wide = false;
   if (want == stereo_mode && wide == wide_mode)
      return false;
   stereo_apply();
   return true;
}

/* current overall 3D amount in % (settings strength x 3D slider) */
static int stereo_amount_pct(void)
{
   return stereo_mode ? (int)(d3.pct * osGet3DSliderState() + 0.5f) : 0;
}

/* Per frame: depth of each BG/OBJ priority, scaled by strength and the
 * 3D slider. Sprites can sit a little nearer than BGs of their priority. */
static void stereo_update(void)
{
   float k;
   int bg[4], obj[4], n, any = 0;
   if (!stereo_mode)
      return;
   video_thread_set_smart_layers(d3.smart);
   video_thread_set_fast3d(d3.fast);
   k = d3.pct / 100.0f * osGet3DSliderState();
   for (n = 0; n < 4; n++)
   {
      float vb = (d3.layer[n] + d3.focus) * k;
      float vo = (d3.layer[n] + d3.sprite + d3.focus) * k;
      bg[n]  = clampi((int)(vb < 0 ? vb - 0.5f : vb + 0.5f), -16, 16);
      obj[n] = clampi((int)(vo < 0 ? vo - 0.5f : vo + 0.5f), -16, 16);
      any |= bg[n] | obj[n];
   }
   {
      float t = d3.tilt * k * 16.0f;
      int t16 = (int)(t < 0 ? t - 0.5f : t + 0.5f);
      video_thread_set_tilt(t16);
      any |= t16;
   }
   /* slider down: draw once, the frame hook shows it to both eyes */
   if (any)
      video_thread_set_stereo2(bg, obj);
   else
      video_thread_set_stereo(NULL);
}

static void video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
   u64 t0;
   (void)width; (void)height;

   if (!data) /* frame skipped by the core: keep showing the last one */
      return;
   if (hook_presented)
   {
      /* core 2 staged it: the GPU draws it and swaps the buffers itself */
      hook_presented = false;
      gpu_request_present();
      return;
   }
   if (hook_blitted)
   {
      /* core 2 already put this frame on screen */
      hook_blitted = false;
      frame_ready = true;
      return;
   }
   t0 = svcGetSystemTick();
   if (GPU_ACTIVE && !video_thread_enabled)
   {
      /* drawn on this core: still let the GPU scale and show it */
      gpu_stage_frame((const u16*)data, NULL, pitch / 2);
      gpu_request_present();
      t_blit_acc += svcGetSystemTick() - t0;
      return;
   }
   blit_to_top((const u16*)data, pitch / 2, GFX_LEFT);
   if (stereo_mode)
      blit_to_top((const u16*)data, pitch / 2, GFX_RIGHT);
   frame_ready = true;
   t_blit_acc += svcGetSystemTick() - t0;
}

/* ------------------------------------------------------------------ */
/* Audio: ndsp, a ring of wave buffers, one per emulated frame         */
/* ------------------------------------------------------------------ */

#define AUDIO_RATE     32768
#define AUDIO_NBUF     12
#define AUDIO_MAXFRAME 2048       /* stereo frames per buffer */
#define AUDIO_TARGET   5          /* frames of sound queued before we wait (~83 ms) */

static bool        audio_ok;
static ndspWaveBuf wbuf[AUDIO_NBUF];
static s16        *wdata;
static int         wnext;
static s16         pending[AUDIO_MAXFRAME * 2];
static size_t      pending_frames;
static retro_audio_buffer_status_callback_t audio_status_cb;
static u32         audio_underruns;   /* queue was empty when a frame arrived */
static u32         audio_drops;       /* queue full, frame's sound dropped */
static int         audio_min_queue = 99;

static void audio_init(void)
{
   int i;
   if (R_FAILED(ndspInit()))
      return;   /* no dspfirm.cdc: run without sound */
   wdata = (s16*)linearAlloc(AUDIO_NBUF * AUDIO_MAXFRAME * 4);
   if (!wdata)
   {
      ndspExit();
      return;
   }
   memset(wdata, 0, AUDIO_NBUF * AUDIO_MAXFRAME * 4);
   ndspSetOutputMode(NDSP_OUTPUT_STEREO);
   ndspChnReset(0);
   ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
   ndspChnSetRate(0, AUDIO_RATE);
   ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
   memset(wbuf, 0, sizeof(wbuf));
   for (i = 0; i < AUDIO_NBUF; i++)
   {
      wbuf[i].data_pcm16 = wdata + i * AUDIO_MAXFRAME * 2;
      wbuf[i].status     = NDSP_WBUF_DONE;
   }
   audio_ok = true;
}

static int audio_queued(void)
{
   int i, n = 0;
   for (i = 0; i < AUDIO_NBUF; i++)
      if (wbuf[i].status == NDSP_WBUF_QUEUED || wbuf[i].status == NDSP_WBUF_PLAYING)
         n++;
   return n;
}

static size_t audio_batch(const int16_t *data, size_t frames)
{
   size_t room = AUDIO_MAXFRAME - pending_frames;
   if (frames > room)
      frames = room;
   memcpy(pending + pending_frames * 2, data, frames * 4);
   pending_frames += frames;
   return frames;
}

static void audio_sample(int16_t l, int16_t r)
{
   int16_t s[2] = { l, r };
   audio_batch(s, 1);
}

static void audio_submit(void)
{
   ndspWaveBuf *b;
   int queued;

   if (!audio_ok || !pending_frames)
   {
      pending_frames = 0;
      return;
   }

   queued = audio_queued();
   if (queued == 0)
      audio_underruns++;
   if (queued < audio_min_queue)
      audio_min_queue = queued;

   b = &wbuf[wnext];
   if (b->status == NDSP_WBUF_QUEUED || b->status == NDSP_WBUF_PLAYING)
   {
      /* every buffer is still queued: we are ahead, drop this frame's audio */
      audio_drops++;
      pending_frames = 0;
      return;
   }

   memcpy((void*)b->data_pcm16, pending, pending_frames * 4);
   b->nsamples = pending_frames;
   flush_dsp(b->data_pcm16, pending_frames * 4);
   ndspChnWaveBufAdd(0, b);
   wnext = (wnext + 1) % AUDIO_NBUF;
   pending_frames = 0;

   if (audio_status_cb)
   {
      queued = audio_queued();
      audio_status_cb(true, (unsigned)(queued * 100 / AUDIO_NBUF), queued <= 1);
   }
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

static u32 keys_held;

static void input_poll(void) { /* polled once per frame in the main loop */ }

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   u32 k = keys_held;
   int16_t m = 0;
   (void)index;
   if (port != 0 || device != RETRO_DEVICE_JOYPAD)
      return 0;
   if (k & KEY_B)      m |= 1 << RETRO_DEVICE_ID_JOYPAD_B;
   if (k & KEY_A)      m |= 1 << RETRO_DEVICE_ID_JOYPAD_A;
   if (k & KEY_SELECT) m |= 1 << RETRO_DEVICE_ID_JOYPAD_SELECT;
   if (k & KEY_START)  m |= 1 << RETRO_DEVICE_ID_JOYPAD_START;
   if (k & KEY_UP)     m |= 1 << RETRO_DEVICE_ID_JOYPAD_UP;
   if (k & KEY_DOWN)   m |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
   if (k & KEY_LEFT)   m |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
   if (k & KEY_RIGHT)  m |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
   if (k & KEY_L)      m |= 1 << RETRO_DEVICE_ID_JOYPAD_L;
   if (k & KEY_R)      m |= 1 << RETRO_DEVICE_ID_JOYPAD_R;
   if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
      return m;
   return (m >> id) & 1;
}

/* ------------------------------------------------------------------ */
/* libretro environment                                                */
/* ------------------------------------------------------------------ */

static char core_msg[96];
static bool core_msg_new;

/* Core warnings are kept in memory and written to the log file when the menu
 * opens or the game closes: opening a file on the SD card can take tens of
 * milliseconds, which froze the game for a moment every time a warning came
 * up during play. */
static char   log_buf[8192];
static size_t log_len;
static u32    log_dropped;

static void log_flush(void)
{
   FILE *f;
   if (!log_len && !log_dropped)
      return;
   f = fopen(APP_DIR "/gpsp3d.log", "a");
   if (f)
   {
      fwrite(log_buf, 1, log_len, f);
      if (log_dropped)
         fprintf(f, "(%lu more messages not logged)\n", (unsigned long)log_dropped);
      fclose(f);
   }
   log_len = 0;
   log_dropped = 0;
}

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
   char buf[256];
   va_list ap;
   int n;
   if (level < RETRO_LOG_WARN)
      return;
   va_start(ap, fmt);
   n = vsnprintf(buf, sizeof(buf), fmt, ap);
   va_end(ap);
   if (n < 0)
      return;
   if (n >= (int)sizeof(buf))
      n = sizeof(buf) - 1;
   if (log_len + n > sizeof(log_buf))
   {
      log_dropped++;
      return;
   }
   memcpy(log_buf + log_len, buf, n);
   log_len += n;
}

static const char *var_value(const char *key)
{
   if (!strcmp(key, "gpsp_drc"))              return "enabled";
   if (!strcmp(key, "gpsp_bios"))             return "auto";
   if (!strcmp(key, "gpsp_boot_mode"))        /* the intro is in Nintendo's BIOS only */
      return opt_intro && file_exists(APP_DIR "/gba_bios.bin") ? "bios" : "game";
   if (!strcmp(key, "gpsp_sound_rate"))       return "32768";
   if (!strcmp(key, "gpsp_frameskip"))        return "disabled";   /* our own, see frame_should_skip */
   if (!strcmp(key, "gpsp_profile"))          return opt_profiler ? "enabled" : "disabled";
   if (!strcmp(key, "gpsp_color_correction")) return "disabled"; /* done in blit_to_top */
   if (!strcmp(key, "gpsp_frame_mixing"))     return "disabled";
   if (!strcmp(key, "gpsp_sprlim"))           return "disabled";
   if (!strcmp(key, "gpsp_rtc"))              return opt_rtc == 1 ? "enabled" : opt_rtc == 2 ? "disabled" : "auto";
   if (!strcmp(key, "gpsp_rtc_time_source"))  return "system";
   if (!strcmp(key, "gpsp_rumble"))           return "disabled";
   if (!strcmp(key, "gpsp_serial"))           return "disabled";
   return NULL;
}

static bool environment(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback*)data)->log = core_log;
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *v = (struct retro_variable*)data;
         v->value = var_value(v->key);
         return v->value != NULL;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool*)data = vars_changed;
         vars_changed = false;
         return true;
      case RETRO_ENVIRONMENT_SET_VARIABLES:
         return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         return *(enum retro_pixel_format*)data == RETRO_PIXEL_FORMAT_RGB565;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char**)data = APP_DIR;
         return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char**)data = save_dir[0] ? save_dir : APP_DIR;
         return true;
      case RETRO_ENVIRONMENT_SET_MESSAGE:
      {
         const struct retro_message *m = (const struct retro_message*)data;
         if (m && m->msg)
         {
            snprintf(core_msg, sizeof(core_msg), "%s", m->msg);
            core_msg_new = true;
         }
         return true;
      }
      case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
         return true;
      case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
      {
         const struct retro_audio_buffer_status_callback *c =
            (const struct retro_audio_buffer_status_callback*)data;
         audio_status_cb = c ? c->callback : NULL;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
         return true;
      default:
         return false;
   }
}

/* ------------------------------------------------------------------ */
/* Saves                                                               */
/* ------------------------------------------------------------------ */

u32 gpsp_backup_size(void);

static const char *save_ext(void) { return opt_save_ext[0] ? opt_save_ext : "srm"; }

/* Another place the game's save may be: where it went before save_dir or
 * save_ext were changed (RetroArch's folder or next to the ROM, .srm).
 * *srm is set when the file found is a .srm. */
static FILE *sram_open_old(bool *srm)
{
   const char *slash = strrchr(rom_path, '/');
   const char *exts[3] = { save_ext(), "srm", "sav" };
   char rom_dir[512];
   const char *dirs[5] = { save_dir, old_save_dir, rom_dir, RA_SAVE_DIR, RA_SAVE_DIR2 };
   char p[800];
   int d, e;
   snprintf(rom_dir, sizeof(rom_dir), "%.*s", slash ? (int)(slash - rom_path) : 0, rom_path);
   for (d = 0; d < 5; d++)
      for (e = 0; e < 3; e++)
      {
         FILE *f;
         snprintf(p, sizeof(p), "%s/%s.%s", dirs[d], rom_name, exts[e]);
         if (strcmp(p, save_path) && (f = fopen(p, "rb")))
         {
            *srm = !strcasecmp(exts[e], "srm");
            return f;
         }
      }
   return NULL;
}

static void sram_load(void)
{
   void  *p = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
   size_t n = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
   FILE  *f;
   bool   srm = !strcasecmp(save_ext(), "srm");
   sram_loaded = 0;
   if (!p || !n)
      return;
   /* a save found in an old place is written to save_path once it changes */
   f = fopen(save_path, "rb");
   if (!f)
      f = sram_open_old(&srm);
   if (f)
   {
      size_t got = fread(p, 1, n, f);
      if (!srm)   /* .srm files are always padded to 128KiB */
         sram_loaded = got;
      fclose(f);
   }
   sram_hash = hash_mem(p, n);
}

/* How much of the save memory to write. .srm is always the full 128KiB
 * (like RetroArch); other extensions (.sav for mGBA, open_agb_firm) get the
 * cartridge's real size, never less than the file had (an 8KiB EEPROM reads
 * as 512 bytes until the game uses it). 0: the game has no save yet. */
static size_t sram_file_size(size_t n)
{
   size_t real;
   if (!strcasecmp(save_ext(), "srm"))
      return n;
   real = gpsp_backup_size();
   if (real < sram_loaded)
      real = sram_loaded;
   return real < n ? real : n;
}

static void sram_flush(bool force)
{
   void  *p = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
   size_t n = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
   u32    h;
   FILE  *f;
   if (!game_loaded || !p || !n)
      return;
   h = hash_mem(p, n);
   /* .sav is only written once the game has changed it: by then the game
    * has used its save memory, so its real size is known */
   if (h == sram_hash && (!force || strcasecmp(save_ext(), "srm")))
      return;
   if (!sram_file_size(n))
      return;
   f = fopen(save_path, "wb");
   if (f)
   {
      fwrite(p, 1, sram_file_size(n), f);
      fclose(f);
      sram_hash = h;
   }
}

/* Whether each state slot has a file: 0 unknown, 1 used, 2 empty. Looking
 * on the SD card takes milliseconds (more in a big saves folder), and the
 * menu asks every frame, so the answer is kept until the game changes. */
static u8 state_known[10];

static bool state_save(void)
{
   FILE *f;
   if (!state_buf || !retro_serialize(state_buf, state_size))
      return false;
   f = fopen(state_path, "wb");
   if (!f)
      return false;
   fwrite(state_buf, 1, state_size, f);
   fclose(f);
   if (opt_slot >= 0 && opt_slot < 10)
      state_known[opt_slot] = 1;
   return true;
}

static bool state_load(void)
{
   FILE  *f = fopen(state_path, "rb");
   size_t n;
   if (!f || !state_buf)
   {
      if (f)
         fclose(f);
      return false;
   }
   n = fread(state_buf, 1, state_size, f);
   fclose(f);
   return n == state_size && retro_unserialize(state_buf, state_size);
}

/* Bottom screen while playing (System > Bottom screen): after 10 s without a
 * touch the stats screen gives way to a plain grey one ("Tap for stats and
 * menu"), or its backlight is turned off (saves battery). A tap wakes it; a
 * guide being read keeps it on. */
#define BOTTOM_IDLE_TICKS ((u64)(TICKS_PER_MS * 10000))
static bool    bottom_asleep, bottom_lcd_off;

/* The bottom backlight, through gsp::Lcd. The session is opened only for
 * the call: holding it the whole time left the HOME Menu waiting for one
 * when HOME was pressed (it froze). */
static bool lcd_backlight(bool on)
{
   Result r;
   if (R_FAILED(gspLcdInit()))
      return false;
   r = on ? GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM) : GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
   gspLcdExit();
   return R_SUCCEEDED(r);
}
static u64     bottom_idle_t0;
static ui_hud  hud_last;
static bool    hud_have;

static void hud_show(void)
{
   if (guide_shown || !hud_have || bottom_lcd_off)
      return;
   if (bottom_asleep)
   {
      ui_clear(UI_RGB(0x1A, 0x1D, 0x24));
      ui_text_center(&ui_font_body, UI_W / 2, UI_H / 2 - 10, "Tap for stats and menu", UI_RGB(0x6B, 0x74, 0x87));
   }
   else
      scr_hud(&hud_last);
   ui_present();
}

static void bottom_wake(void)
{
   bottom_idle_t0 = svcGetSystemTick();
   if (!bottom_asleep)
      return;
   bottom_asleep = false;
   if (bottom_lcd_off)
   {
      lcd_backlight(true);
      bottom_lcd_off = false;
   }
   hud_show();
}

static void bottom_check_idle(void)
{
   if (!opt_bottom || bottom_asleep || guide_shown ||
       svcGetSystemTick() - bottom_idle_t0 < BOTTOM_IDLE_TICKS)
      return;
   bottom_asleep = true;
   if (opt_bottom == 2 && lcd_backlight(false))
      bottom_lcd_off = true;
   else
      hud_show();   /* the plain screen */
}

static void apt_hook(APT_HookType hook, void *param)
{
   (void)param;
   /* the HOME Menu and the closing app need the bottom screen lit */
   if ((hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONEXIT) && bottom_lcd_off)
   {
      lcd_backlight(true);
      bottom_lcd_off = false;
   }
   if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONWAKEUP)
      bottom_asleep = false;   /* starts counting again when the game is back */
   if (hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP)
      bottom_idle_t0 = svcGetSystemTick();
   if (hook == APTHOOK_ONSUSPEND || hook == APTHOOK_ONSLEEP || hook == APTHOOK_ONEXIT)
      sram_flush(false);
   /* Give core 2 back to the system while we're in the background: with
    * 80% reserved for us, the HOME menu (which runs on that core) crawls.
    * Not for sleep: nothing runs then, and this is a call to the system's
    * APT service in the middle of its sleep handshake. */
   if (hook == APTHOOK_ONSUSPEND && video_thread_enabled)
      APT_SetAppCpuTimeLimit(30);
   else if (hook == APTHOOK_ONRESTORE && video_thread_enabled)
      APT_SetAppCpuTimeLimit(80);
}

/* ------------------------------------------------------------------ */
/* ROM browser (bottom screen)                                         */
/* ------------------------------------------------------------------ */

typedef struct { char name[256]; bool dir; } entry_t;

static entry_t *entries;
static int      n_entries;

static bool is_rom(const char *name)
{
   const char *ext = strrchr(name, '.');
   return ext && (!strcasecmp(ext, ".gba") || !strcasecmp(ext, ".agb") ||
                  !strcasecmp(ext, ".bin") || !strcasecmp(ext, ".zip"));
}

static int entry_cmp(const void *a, const void *b)
{
   const entry_t *x = (const entry_t*)a, *y = (const entry_t*)b;
   if (x->dir != y->dir)
      return x->dir ? -1 : 1;
   return strcasecmp(x->name, y->name);
}

static void scan_dir(void)
{
   DIR *d;
   struct dirent *e;
   int cap = 64;

   free(entries);
   entries   = (entry_t*)malloc(cap * sizeof(entry_t));
   n_entries = 0;
   if (strcmp(cur_dir, "sdmc:/"))
   {
      strcpy(entries[0].name, "..");
      entries[0].dir = true;
      n_entries = 1;
   }
   d = opendir(cur_dir);
   if (!d)
      return;
   while ((e = readdir(d)))
   {
      bool isdir = e->d_type == DT_DIR;
      if (e->d_name[0] == '.')
         continue;
      if (!isdir && !is_rom(e->d_name))
         continue;
      if (n_entries == cap)
      {
         entry_t *n;
         cap *= 2;
         n = (entry_t*)realloc(entries, cap * sizeof(entry_t));
         if (!n)
            break;
         entries = n;
      }
      snprintf(entries[n_entries].name, sizeof(entries[n_entries].name), "%s", e->d_name);
      entries[n_entries].dir = isdir;
      n_entries++;
   }
   closedir(d);
   qsort(entries + (strcmp(cur_dir, "sdmc:/") ? 1 : 0),
         n_entries - (strcmp(cur_dir, "sdmc:/") ? 1 : 0), sizeof(entry_t), entry_cmp);
}

static void path_up(char *p)
{
   size_t n = strlen(p);
   if (n > 6 && p[n - 1] == '/')
      p[--n] = '\0';
   while (n > 6 && p[n - 1] != '/')
      p[--n] = '\0';
}

/* Returns true when a ROM was picked (rom_path filled), false to quit. */
static void browse_draw(int sel, int top)
{
   static const char **names;
   static unsigned char *isdir;
   static int cap;
   int i;
   if (n_entries > cap)
   {
      cap = n_entries + 64;
      names = (const char**)realloc(names, cap * sizeof(*names));
      isdir = (unsigned char*)realloc(isdir, cap);
   }
   for (i = 0; i < n_entries; i++)
   {
      names[i] = entries[i].name;
      isdir[i] = entries[i].dir;
   }
   scr_browser(cur_dir + 5, names, isdir, n_entries, sel, top);
   ui_present();
}

static bool browse_open(int sel)
{
   entry_t *e = &entries[sel];
   if (e->dir)
   {
      if (!strcmp(e->name, ".."))
         path_up(cur_dir);
      else
      {
         size_t n = strlen(cur_dir);
         snprintf(cur_dir + n, sizeof(cur_dir) - n, "%s%s/", n && cur_dir[n - 1] == '/' ? "" : "/", e->name);
      }
      scan_dir();
      return false;
   }
   snprintf(rom_path, sizeof(rom_path), "%s%s", cur_dir, e->name);
   config_save();
   return true;
}

static bool browse(void)
{
   int sel = 0, top = 0;
   bool redraw = true, touch_down = false;
   int touch_y0 = 0, touch_idx = -1, moved = 0, hold_frames = 0;

   scan_dir();
   while (aptMainLoop())
   {
      u32 down, held;
      int step;
      hidScanInput();
      down = hidKeysDown() | (hidKeysDownRepeat() & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT));
      held = hidKeysHeld();
      /* holding Up / Down scrolls faster and faster: 1 row per repeat,
       * then 2 after 1 second, 4 after 2 seconds (long game lists) */
      hold_frames = (held & (KEY_UP | KEY_DOWN)) ? hold_frames + 1 : 0;
      step = (hidKeysDown() & (KEY_UP | KEY_DOWN)) ? 1 : hold_frames > 120 ? 4 : hold_frames > 60 ? 2 : 1;

      if (down & KEY_START)
         return false;
      if (down & KEY_DOWN)  { sel += step; redraw = true; }
      if (down & KEY_UP)    { sel -= step; redraw = true; }
      if (down & KEY_RIGHT) { sel += LIST_ROWS; redraw = true; }
      if (down & KEY_LEFT)  { sel -= LIST_ROWS; redraw = true; }

      if ((down & KEY_B) && strcmp(cur_dir, "sdmc:/"))
      {
         path_up(cur_dir);
         scan_dir();
         sel = top = 0;
         redraw = true;
      }
      if ((down & KEY_A) && n_entries)
      {
         if (browse_open(sel))
            return true;
         sel = top = 0;
         redraw = true;
      }

      /* touch: tap a game to select it, tap it again to open; drag scrolls */
      if (held & KEY_TOUCH)
      {
         touchPosition tp;
         hidTouchRead(&tp);
         if (!touch_down)
         {
            touch_down = true;
            touch_y0 = tp.py;
            moved = 0;
            touch_idx = scr_browser_hit(tp.py, top, n_entries);
         }
         else if (abs(tp.py - touch_y0) >= LIST_ROW_H)
         {
            int d = (touch_y0 - tp.py) / LIST_ROW_H;
            top += d;
            sel += d;
            touch_y0 -= d * LIST_ROW_H;
            moved = 1;
            redraw = true;
         }
      }
      else if (touch_down)
      {
         touch_down = false;
         if (!moved && touch_idx >= 0)
         {
            if (touch_idx == sel)
            {
               if (browse_open(sel))
                  return true;
               sel = top = 0;
            }
            else
               sel = touch_idx;
            redraw = true;
         }
      }

      if (sel >= n_entries) sel = n_entries - 1;
      if (sel < 0)          sel = 0;
      if (redraw)
      {
         if (top > n_entries - LIST_ROWS) top = n_entries - LIST_ROWS;
         if (top < 0) top = 0;
         if (sel < top) top = sel;
         if (sel >= top + LIST_ROWS) top = sel - LIST_ROWS + 1;
         browse_draw(sel, top);
         redraw = false;
      }
      gspWaitForVBlank();
   }
   return false;
}

/* ------------------------------------------------------------------ */
/* Game                                                                */
/* ------------------------------------------------------------------ */

/* RetroArch naming: slot 0 = name.state, slot N = name.stateN */
static void state_path_for(int slot, char *out, size_t n)
{
   if (slot)
      snprintf(out, n, "%s/%s.state%d", state_dir, rom_name, slot);
   else
      snprintf(out, n, "%s/%s.state", state_dir, rom_name);
}
static void make_state_path(void) { state_path_for(opt_slot, state_path, sizeof(state_path)); }
static bool state_exists(int slot)
{
   char p[600];
   bool used;
   if (slot >= 0 && slot < 10 && state_known[slot])
      return state_known[slot] == 1;
   state_path_for(slot, p, sizeof(p));
   used = file_exists(p);
   if (slot >= 0 && slot < 10)
      state_known[slot] = used ? 1 : 2;
   return used;
}

static void profile_load_fwd(void);
static void profile_leave_fwd(void);

static void make_paths(void)
{
   const char *slash = strrchr(rom_path, '/');
   const char *dot;
   char rom_dir[512];
   char try_path[600];
   bool beside;
   size_t dl = slash ? (size_t)(slash - rom_path + 1) : 0;

   snprintf(rom_name, sizeof(rom_name), "%s", slash ? slash + 1 : rom_path);
   dot = strrchr(rom_name, '.');
   if (dot)
      rom_name[dot - rom_name] = '\0';
   snprintf(rom_dir, sizeof(rom_dir), "%.*s", (int)dl, rom_path);

   /* Saves live where RetroArch keeps them (so saves carry over between the
    * two), unless the game already has a save next to the ROM. */
   snprintf(try_path, sizeof(try_path), "%s%s.srm", rom_dir, rom_name);
   beside = file_exists(try_path);
   if (!beside && opt_save_ext[0])   /* e.g. an mGBA .sav next to the game */
   {
      snprintf(try_path, sizeof(try_path), "%s%s.%s", rom_dir, rom_name, opt_save_ext);
      beside = file_exists(try_path);
   }
   if (!beside && dir_exists(RA_SAVE_DIR))
      snprintf(old_save_dir, sizeof(old_save_dir), "%s", RA_SAVE_DIR);
   else if (!beside && dir_exists(RA_SAVE_DIR2))
      snprintf(old_save_dir, sizeof(old_save_dir), "%s", RA_SAVE_DIR2);
   else
      snprintf(old_save_dir, sizeof(old_save_dir), "%.*s", (int)(dl ? dl - 1 : 0), rom_path);

   /* gpsp3d.cfg can name other folders (e.g. one shared with mGBA or
    * open_agb_firm); states go with the saves unless they have their own */
   if (opt_save_dir[0])
   {
      mkdir_path(opt_save_dir);
      snprintf(save_dir, sizeof(save_dir), "%s", opt_save_dir);
   }
   else
      snprintf(save_dir, sizeof(save_dir), "%s", old_save_dir);
   if (opt_state_dir[0])
   {
      mkdir_path(opt_state_dir);
      snprintf(state_dir, sizeof(state_dir), "%s", opt_state_dir);
   }
   else
      snprintf(state_dir, sizeof(state_dir), "%s", save_dir);

   snprintf(save_path, sizeof(save_path), "%s/%s.%s", save_dir, rom_name, save_ext());
   memset(state_known, 0, sizeof(state_known));
   make_state_path();
}

/* Gyro sensor games (WarioWare: Twisted): the 3DS gyroscope stands in for
 * the cartridge's. Same mapping as mGBA's 3DS port (raw rate in shared
 * memory, the "y" entry is the rotation in the screen's plane). */
extern int gpsp_gyro_z;
int gpsp_gyro_present(void);
/* tilt sensor carts (Yoshi Topsy-Turvy, Koro Koro Puzzle): the 3DS
 * accelerometer, scaled like mGBA's 3DS port */
extern int gpsp_tilt_x, gpsp_tilt_y;
int gpsp_tilt_present(void);
static bool tilt_on;
static bool gyro_on;
static void gyro_update(void)
{
   if (tilt_on)
   {
      /* read straight from shared memory, like the gyro below (and mGBA) */
      accelVector a = *(const accelVector*)(uintptr_t)&hidSharedMem[0x48];
      gpsp_tilt_x = a.x >> 3;
      gpsp_tilt_y = a.y >> 3;
   }
   if (!gyro_on)
      return;
   {
      angularRate g = *(const angularRate*)(uintptr_t)&hidSharedMem[0x5C];
      gpsp_gyro_z = g.y >> 4;
   }
}

/* the gpSP core loads only plain ROM files: a .zip is unpacked first (to a
 * cache on the SD card; saves are still named and placed after the zip) */
static char load_err[200];

static void unzip_message(uint32_t size)
{
   char body[96];
   snprintf(body, sizeof(body), "%s\n%.1f MB, once: starting it again is instant.",
            rom_name, size / 1048576.0);
   scr_message("Unzipping the game...", body, NULL);
   ui_present();
}

static bool game_load(void)
{
   struct retro_game_info info;
   static char unzipped[700];
   const char *ext;
   make_paths();
   memset(&info, 0, sizeof(info));
   info.path = rom_path;
   load_err[0] = '\0';
   ext = strrchr(rom_path, '.');
   if (ext && !strcasecmp(ext, ".zip"))
   {
      if (unzip_rom(rom_path, APP_DIR "/unzipped", rom_name, unzipped, sizeof(unzipped),
                    unzip_message, load_err, sizeof(load_err)))
         return false;
      info.path = unzipped;
   }
   vars_changed = true;
   if (!retro_load_game(&info))
      return false;
   game_loaded = true;
   gpsp_gyro_z = 0;
   gyro_on = gpsp_gyro_present() && R_SUCCEEDED(HIDUSER_EnableGyroscope());
   gpsp_tilt_x = gpsp_tilt_y = 0;
   tilt_on = gpsp_tilt_present() && R_SUCCEEDED(HIDUSER_EnableAccelerometer());
   sram_load();
   profile_load_fwd();
   guide_open(rom_path, rom_name, APP_DIR);
   log_flush();   /* warnings from loading, before the game starts */
   return true;
}

static void game_unload(void)
{
   if (!game_loaded)
      return;
   sram_flush(true);
   guide_close();
   retro_unload_game();
   log_flush();
   game_loaded = false;
   if (gyro_on)
   {
      HIDUSER_DisableGyroscope();
      gyro_on = false;
   }
   if (tilt_on)
   {
      HIDUSER_DisableAccelerometer();
      tilt_on = false;
   }
   profile_leave_fwd();
}

/* ------------------------------------------------------------------ */
/* Second-core test: how much of the Old 3DS system core an app gets    */
/* ------------------------------------------------------------------ */

static volatile bool bench_stop;
static volatile u32  bench_sink;
static u32           bench_count1;

static u32 bench_work(void)
{
   u32 x = 12345, i;
   for (i = 0; i < 4096; i++)
      x = x * 1664525u + 1013904223u;
   return x;
}

static void bench_thread(void *arg)
{
   u32 n = 0;
   (void)arg;
   while (!bench_stop)
   {
      bench_sink ^= bench_work();
      n++;
   }
   bench_count1 = n;
}

/* Runs the same loop on both cores at once for about a second and returns
 * the second core's speed as a percentage of the main core's. */
static void core2_test(char *out, size_t outlen)
{
   static const u32 limits[] = { 30, 50, 80 };
   char tmp[48];
   size_t i;
   u32 old_limit = 0;
   APT_GetAppCpuTimeLimit(&old_limit);
   out[0] = '\0';
   for (i = 0; i < sizeof(limits) / sizeof(limits[0]); i++)
   {
      Result rc = APT_SetAppCpuTimeLimit(limits[i]);
      u32 n0 = 0;
      u64 t0;
      Thread t;
      bench_stop   = false;
      bench_count1 = 0;
      t = threadCreate(bench_thread, NULL, 0x4000, 0x18, 1, false);
      if (!t)
      {
         snprintf(tmp, sizeof(tmp), "%lu%%: no thread (rc %08lX) ", (unsigned long)limits[i], (unsigned long)rc);
         strncat(out, tmp, outlen - strlen(out) - 1);
         continue;
      }
      t0 = svcGetSystemTick();
      while (svcGetSystemTick() - t0 < SYSCLOCK_ARM11)
      {
         bench_sink ^= bench_work();
         n0++;
      }
      bench_stop = true;
      threadJoin(t, U64_MAX);
      threadFree(t);
      snprintf(tmp, sizeof(tmp), "%lu%%->%lu%% ", (unsigned long)limits[i],
               (unsigned long)(n0 ? (u64)bench_count1 * 100 / n0 : 0));
      strncat(out, tmp, outlen - strlen(out) - 1);
   }
   /* the threaded renderer needs its 80% back */
   APT_SetAppCpuTimeLimit(video_thread_enabled ? 80 : (old_limit ? old_limit : 30));
}

static void core2_apply(void)
{
   if (opt_core2 && !video_thread_enabled)
   {
      video_thread_set_low_priority(opt_lowprio);
      if (video_thread_start(1) != 0)
         opt_core2 = 0;   /* could not start: keep drawing on the main core */
      else
         video_thread_set_frame_hook(frame_hook, NULL);   /* screen copy on core 2 too */
   }
   else if (!opt_core2 && video_thread_enabled)
      video_thread_stop();
}

static void d3_set_style(int style)
{
   int i;
   d3.style = style;
   if (style >= D3_STYLES - 1)
      return;   /* Custom keeps the current values */
   for (i = 0; i < 4; i++)
      d3.layer[i] = d3_styles[style][i];
   d3.sprite = d3_styles[style][4];
}

/* ------------------------------------------------------------------ */
/* Per-game 3D profiles: sdmc:/3ds/gpsp3d/games/<game>.cfg              */
/* ------------------------------------------------------------------ */

static void profile_path(char *out, size_t n)
{
   snprintf(out, n, "%s/games/%s.cfg", APP_DIR, rom_name);
}

static void profile_save(void);

/* on game load: use the game's own 3D settings if it has a profile */
static void profile_load(void)
{
   char p[600], line[200];
   FILE *f;
   d3_global = d3;
   d3_pergame = false;
   profile_path(p, sizeof(p));
   f = fopen(p, "r");
   if (!f)
   {
      /* No profile yet. Games that build their map from several BG layers
       * (Pokemon) show the hidden filler tiles under walls when those layers
       * get different depths. Smart layers keeps them together; start them on
       * Deep (looks best there), saved for this game. */
      char title[13] = "";
      FILE *rf = fopen(rom_path, "rb");
      if (rf)
      {
         if (fseek(rf, 0xA0, SEEK_SET) == 0)
            title[fread(title, 1, 12, rf)] = 0;
         fclose(rf);
      }
      if (!strncmp(title, "POKEMON", 7))
      {
         d3_set_style(2);   /* Deep */
         d3.smart = 1;
         d3_pergame = true;
         profile_save();
      }
      return;
   }
   while (fgets(line, sizeof(line), f))
   {
      char *eq = strchr(line, '='), *nl = strpbrk(line, "\r\n");
      if (nl) *nl = 0;
      if (!eq) continue;
      *eq++ = 0;
      d3_parse(line, eq, &d3);
   }
   fclose(f);
   d3_pergame = true;
}

static void profile_save(void)
{
   char p[600];
   FILE *f;
   if (!d3_pergame || !rom_name[0])
      return;
   mkdir(APP_DIR "/games", 0777);
   profile_path(p, sizeof(p));
   f = fopen(p, "w");
   if (!f)
      return;
   d3_write(f, &d3);
   fclose(f);
}

/* on game unload: back to the all-games settings */
static void profile_leave(void)
{
   if (d3_pergame)
   {
      profile_save();
      d3 = d3_global;
      d3_pergame = false;
   }
}

/* ------------------------------------------------------------------ */
/* Bottom-screen helpers                                               */
/* ------------------------------------------------------------------ */

static void ui_wait_message(const char *title, const char *body)
{
   bool was_touch = true;
   scr_message(title, body, "Press A or tap to continue");
   ui_present();
   while (aptMainLoop())
   {
      u32 held;
      hidScanInput();
      held = hidKeysHeld();
      if (hidKeysDown() & KEY_A)
         break;
      if (held & KEY_TOUCH) { if (!was_touch) break; was_touch = true; }
      else was_touch = false;
      gspWaitForVBlank();
   }
}

/* Show the paused game's last frame again with the current 3D / colour /
 * size settings, so changes in the menu can be seen right away. */
static void preview_refresh(void)
{
   /* mode first (3D / 2D / 800px), then clear in that mode's layout */
   stereo_apply();
   clear_top();
   if (!video_thread_enabled || !game_loaded)
      return;
   stereo_update();
   if (!video_thread_redraw())
      return;
   if (hook_presented)
   {
      hook_presented = false;
      gpu_request_present();
      present_idle();
   }
   else if (hook_blitted)
   {
      hook_blitted = false;
      flush_gpu(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), TOP_FB_BYTES);
      if (stereo_mode)
         flush_gpu(gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), TOP_FB_BYTES);
      gfxScreenSwapBuffers(GFX_TOP, stereo_mode);
   }
}

/* ------------------------------------------------------------------ */
/* In-game menu                                                        */
/* ------------------------------------------------------------------ */

enum {
   M_NONE,
   /* Game */
   M_RESUME, M_SLOT, M_SAVE, M_LOAD, M_RESET, M_GUIDE, M_LIST, M_QUIT,
   /* Display */
   M_SIZE, M_SMOOTH, M_CRT, M_CSTYLE, M_SAT, M_BRIGHT, M_COLRESET,
   /* 3D */
   M_3D, M_3DPCT, M_3DMETHOD, M_3DSMART, M_3DSTYLE, M_L0, M_L1, M_L2, M_L3, M_SPRITE, M_FOCUS, M_TILT, M_GAPFILL, M_PERGAME, M_3DRESET,
   /* Speed */
   M_SKIP, M_ASTRETCH, M_FFMODE, M_FFSPEED, M_STATS,
   /* System */
   M_CORE2, M_GPU, M_PRIO, M_C2TEST, M_RTC, M_INTRO, M_BOTTOM, M_SAVEINFO, M_ABOUT, M_CREDITS,
   M_HDR
};

static const char *const menu_tabs[] = { "Game", "Display", "3D", "Speed", "System" };
#define MENU_NTABS 5
#define MENU_MAXROWS 16

typedef struct { int id; ui_row r; const char *help; char vbuf[24]; } mrow;
static mrow mrows[MENU_MAXROWS];
static int  mrow_n;
static char core2_result[80];
static int  confirm_id;           /* action waiting for a second press */

static void mrow_add(int id, int kind, const char *label, const char *help)
{
   mrow *m = &mrows[mrow_n++];
   memset(m, 0, sizeof(*m));
   m->id = id;
   m->r.kind = kind;
   m->r.label = label;
   m->help = help;
}

static void mrow_slider(int id, const char *label, int v, int lo, int hi, const char *fmt, const char *help)
{
   mrow *m = &mrows[mrow_n];
   mrow_add(id, ROW_SLIDER, label, help);
   m->r.v = v; m->r.vmin = lo; m->r.vmax = hi;
   snprintf(m->vbuf, sizeof(m->vbuf), fmt, v);
   m->r.value = m->vbuf;
}

static void mrow_choice(int id, const char *label, const char *val, const char *help)
{
   mrow *m = &mrows[mrow_n];
   mrow_add(id, ROW_CHOICE, label, help);
   snprintf(m->vbuf, sizeof(m->vbuf), "%s", val);
   m->r.value = m->vbuf;
}

static void mrow_toggle(int id, const char *label, int on, const char *help)
{
   mrow *m = &mrows[mrow_n];
   mrow_add(id, ROW_TOGGLE, label, help);
   m->r.on = on;
}

static void menu_build(int tab)
{
   static const char *const size_names[] = { "Original", "Fit", "Stretch" };
   mrow_n = 0;
   switch (tab)
   {
      case 0:
      {
         char slot[24];
         snprintf(slot, sizeof(slot), "%d  %s", opt_slot, state_exists(opt_slot) ? "(used)" : "(empty)");
         mrow_add(M_RESUME, ROW_ACTION, "Resume", "Back to the game. B does the same.");
         mrow_choice(M_SLOT, "State slot", slot, "Which slot Save / Load state use.\nSlot 0 is the one RetroArch uses.");
         mrow_add(M_SAVE, ROW_ACTION, "Save state", "Save the game exactly as it is now\nto the selected slot.");
         mrow_add(M_LOAD, ROW_ACTION, "Load state", "Go back to the state saved in the\nselected slot.");
         mrows[mrow_n - 1].r.disabled = !state_exists(opt_slot);
         mrow_add(M_RESET, ROW_ACTION, "Reset game", "Restart the game, like switching the\nGBA off and on.");
         mrows[mrow_n - 1].r.danger = 1;
         if (guide_available())
         {
            mrow_add(M_GUIDE, ROW_ACTION, guide_shown ? "Hide guide" : "Show guide",
                  guide_shown ? "Back to the speed and 3D panel." :
                  "Read this game's guide (a .txt file)\non the bottom screen while you play.");
         }
         else if (guide_error())
         {
            mrow_add(M_GUIDE, ROW_ACTION, "Show guide", guide_error());
            mrows[mrow_n - 1].r.disabled = 1;
         }
         mrow_add(M_LIST, ROW_ACTION, "Back to game list", "Close this game (its save is kept)\nand choose another.");
         mrow_add(M_QUIT, ROW_ACTION, "Quit gpSP 3D", "Close the game and the app.");
         mrows[mrow_n - 1].r.danger = 1;
         {
            static char sv[24];
            snprintf(sv, sizeof(sv), "slot %d", opt_slot);
            mrows[2].r.value = sv;
            mrows[3].r.value = sv;
         }
         break;
      }
      case 1:
         mrow_choice(M_SIZE, "Screen size", size_names[opt_display], "Original: sharp 1:1 pixels.\nFit: 1.5x bigger.  Stretch: fill the screen.");
         {
            static const char *const sc[] = { "Sharp", "Crisp", "Smooth" };
            mrow_choice(M_SMOOTH, "Scaling", sc[opt_smooth],
                  opt_smooth == 0 ? "Sharp: hard, even pixels. In 2D at Fit\nthe columns are exact (800px mode)." :
                  opt_smooth == 1 ? "Crisp: even pixels with slightly soft\nedges (best for Fit / Stretch)." :
                                    "Smooth: soft, blended pixels.");
         }
         {
            static const char *const fx[] = { "Off", "Scanlines", "LCD grid" };
            mrow_choice(M_CRT, "Screen effect", fx[opt_crt],
                  opt_crt == 1 ? "Scanlines: dark lines between the rows,\nlike a CRT TV. Best at Fit / Stretch." :
                  opt_crt == 2 ? "LCD grid: thin gaps around every pixel,\nlike the GBA screen. Best at Fit / Stretch." :
                                 "No effect. (Needs GPU display.)");
         }
         mrow_choice(M_CSTYLE, "Color style", cc_names[opt_colorcorr], "Original: colours as the game made them.\nGBA soft / GBA screen: like the old GBA LCD.");
         mrow_slider(M_SAT, "Saturation", opt_sat, 50, 150, "%d%%", "More or less colourful.\n100% = as the game drew it.");
         mrow_slider(M_BRIGHT, "Brightness", opt_bright, 70, 130, "%d%%", "Lifts or darkens the mid-tones;\nblack and white stay as they are.");
         mrow_add(M_COLRESET, ROW_ACTION, "Reset colours", "Original style, 100% saturation and\nbrightness.");
         break;
      case 2:
      {
         int dis = !d3.on;
         mrow_toggle(M_3D, "3D effect", d3.on, video_thread_enabled ?
               "Each GBA layer at its own depth. Use the\n3DS 3D slider for the amount." :
               "Needs System > Draw on core 2 On.");
         mrow_slider(M_3DPCT, "Strength", d3.pct, 0, 200, "%d%%", "How far the layers separate, on top of\nthe 3D slider. 100% = normal.");
         mrow_choice(M_3DMETHOD, "Method", d3.fast ? "Experimental" : "Normal", d3.fast ?
               "Experimental: draw once, move each layer's\npixels. Usually slower on a 3DS." :
               "Normal: draw each eye separately.\nRecommended.");
         mrow_toggle(M_3DSMART, "Smart layers", d3.smart, "Layers that scroll together (one picture\nsplit in layers, e.g. Pokemon maps) stay flat.");
         mrow_choice(M_3DSTYLE, "Layer style", d3_style_names[d3.style],
               d3.style == 0 ? "Classic: HUD in front, the world behind.\nTip: changes show on the top screen." :
               d3.style == 1 ? "Pop-out: the whole picture comes out of\nthe screen towards you." :
               d3.style == 2 ? "Deep: HUD at the screen, the world far\nbehind it." :
               d3.style == 3 ? "Gentle: a subtle effect, easy on the\neyes." :
               d3.style == 4 ? "Flat map: text in front, the map flat,\ncharacters pop out. Best for Pokemon/RPGs." :
                               "Custom: your own layer depths below.");
         mrow_add(M_HDR, ROW_HEADER, "LAYER DEPTHS  (+ nearer, - further)", NULL);
         mrow_slider(M_L0, "Front layer", d3.layer[0], -8, 8, "%+d", "GBA priority 0: usually the HUD, text\nboxes and menus.");
         mrow_slider(M_L1, "Middle layer", d3.layer[1], -8, 8, "%+d", "GBA priority 1: often characters'\nlayer or foreground scenery.");
         mrow_slider(M_L2, "Back layer", d3.layer[2], -8, 8, "%+d", "GBA priority 2: often the main play\nfield or road.");
         mrow_slider(M_L3, "Far layer", d3.layer[3], -8, 8, "%+d", "GBA priority 3: usually sky and far\nbackgrounds.");
         mrow_slider(M_SPRITE, "Sprite pop", d3.sprite, -4, 4, "%+d", "Characters and objects (sprites) this\nmuch nearer than their layer.");
         mrow_slider(M_FOCUS, "Focus", d3.focus, -6, 6, "%+d", "Moves the whole picture nearer (+) or\nfurther (-) without changing spacing.");
         mrow_slider(M_TILT, "Floor tilt", d3.tilt, -8, 8, "%+d", "Bottom of the screen nearer, top further.\nGives depth to roads in racing games.");
         mrow_toggle(M_GAPFILL, "Fill gaps", opt_gapfill, "Hide thin seams where two layers slide\napart (pink/black lines, e.g. Pokemon).");
         mrow_choice(M_PERGAME, "Settings for", d3_pergame ? "This game" : "All games",
               "This game: keep these 3D settings just\nfor this game (saved per game).");
         mrow_add(M_3DRESET, ROW_ACTION, "Reset 3D", "Back to Classic at 100%, no sprite pop\nor focus.");
         {
            int i;
            for (i = 1; i < mrow_n; i++)
               if (mrows[i].id != M_PERGAME && mrows[i].id != M_HDR)
                  mrows[i].r.disabled = dis;
         }
         break;
      }
      case 3:
         mrow_choice(M_SKIP, "Frame skip", opt_frameskip ? "Auto" : "Off", "Auto: if a frame can't be drawn in time,\nskip drawing it so the game stays at 60.");
         mrow_choice(M_ASTRETCH, "Audio when slow", opt_audio_stretch ? "Stretch" : "Normal", "Stretch: if the game runs slow, slow the\nsound down instead of crackling.");
         {
            static const char *speeds[4] = { "Max", "2x", "3x", "4x" };
            mrow_choice(M_FFMODE, "Fast forward (Y)", opt_ff_toggle ? "Toggle" : "Hold",
                        "Hold: fast while Y is held.\nToggle: Y turns it on and off.");
            mrow_choice(M_FFSPEED, "Fast forward speed", speeds[opt_ff_speed],
                        "Max: as fast as the 3DS can go (light\ngames: 2-4x on Old 3DS, more on New).");
         }
         mrow_toggle(M_STATS, "Show stats", opt_profiler, "Timing numbers on this screen while\nplaying.");
         break;
      case 4:
      {
         static char about[40];
         mrow_toggle(M_CORE2, "Draw on core 2", video_thread_enabled, "Draw the picture on the 3DS's second\nCPU core. Needed for 3D, faster.");
         mrow_toggle(M_GPU, "GPU display", opt_gpu, "Let the 3DS graphics chip scale and\nshow the picture.");
         mrow_choice(M_PRIO, "Core 2 priority", opt_lowprio ? "Low" : "High", "High (recommended): drawing first.\nLow: system services on core 2 first.");
         mrow_add(M_C2TEST, ROW_ACTION, "Core 2 speed test", "Measures how much of core 2 this app\ngets (takes 3 seconds).");
         mrows[mrow_n - 1].r.value = core2_result[0] ? core2_result : NULL;
         {
            static const char *const rtc_names[] = { "Auto", "On", "Off" };
            static const char *const bottom_names[] = { "Always on", "Plain after 10 s", "Off after 10 s" };
            mrow_choice(M_RTC, "Real-time clock", rtc_names[opt_rtc],
                  "On: for Pokemon hacks that say the internal\nbattery has run dry. Applies when a game starts.");
            mrow_choice(M_INTRO, "Startup", opt_intro ? "GBA intro" : "Skip intro",
                  "GBA intro needs your own gba_bios.bin in\nsdmc:/3ds/gpsp3d/. Applies when a game starts.");
            mrow_choice(M_BOTTOM, "Bottom screen", bottom_names[opt_bottom],
                  "Plain: a grey screen. Off: backlight off.\nA tap wakes it. Stays on with a guide open.");
         }
         if (save_path[0])
         {
            /* where this game's save is: the folder, and the full path
             * (folder / file on two lines) in the help line */
            static char where[600];
            const char *slash = strrchr(save_path, '/');
            bool ra = !strncmp(save_path, RA_SAVE_DIR "/", strlen(RA_SAVE_DIR) + 1) ||
                      !strncmp(save_path, RA_SAVE_DIR2 "/", strlen(RA_SAVE_DIR2) + 1);
            snprintf(where, sizeof(where), "%.*s/\n%s", slash ? (int)(slash - save_path) : 0, save_path,
                     slash ? slash + 1 : save_path);
            mrow_add(M_SAVEINFO, ROW_INFO, "Save file", where);
            mrows[mrow_n - 1].r.value = opt_save_dir[0] ? "Your save folder" :
                                        ra ? "RetroArch folder" : "Next to the game";
         }
         snprintf(about, sizeof(about), "gpSP 3D " GPSP3D_VERSION);
         mrow_add(M_ABOUT, ROW_INFO, "Version", "gpSP (libretro, GPL-2), mGBA 3DS GPU\ncode (MPL-2.0), Inter font (OFL).");
         mrows[mrow_n - 1].r.value = about;
         mrow_add(M_CREDITS, ROW_INFO, "Credits", "gpSP: Exophase, notaz, davidgfnet and others.\nThanks: michvllni, hspman69 and all testers.");
         mrows[mrow_n - 1].r.value = "bheppe";
         break;
      }
   }
}

/* apply a change: dir = -1 / +1 for steps, or 0 with a value (sliders) */
/* The live preview behind the menu (re-drawing the game picture, ~10-30 ms
 * on an Old 3DS) waits until the buttons have been still for a few frames,
 * so the menu itself always reacts at once. */
static int  preview_wait;
static bool preview_cc;
static void preview_later(bool colours)
{
   preview_wait = 6;
   if (colours)
      preview_cc = true;
}
static void preview_flush(void)
{
   if (preview_cc)
   {
      cc_rebuild();
      preview_cc = false;
   }
   if (preview_wait)
   {
      preview_wait = 0;
      preview_refresh();
   }
}
static void preview_tick(void)
{
   if (preview_wait && --preview_wait == 0)
   {
      preview_wait = 1;
      preview_flush();
   }
}

static const char *menu_change(int id, int dir, bool has_value, int value)
{
#define STEP(v, lo, hi, st) do { int nv = has_value ? value : (v) + dir * (st); \
      nv = has_value ? (lo) + (((nv) - (lo)) + (st) / 2) / (st) * (st) : nv; \
      v = clampi(nv, lo, hi); } while (0)
#define CYCLEV(v, n) v = ((v) + (dir < 0 ? (n) - 1 : 1)) % (n)
   if (!dir && !has_value)
      dir = 1;
   switch (id)
   {
      case M_SLOT:     CYCLEV(opt_slot, 10); make_state_path(); break;
      case M_SIZE:     CYCLEV(opt_display, 3); preview_later(false); break;
      case M_CRT:      CYCLEV(opt_crt, 3); preview_later(false); break;
      case M_SMOOTH:   CYCLEV(opt_smooth, 3); gpu_apply_filter(); preview_later(false);
                       if (!GPU_ACTIVE) return "Scaling needs System > GPU display On";
                       if (opt_display == 0) return "Original size is always 1:1 sharp";
                       break;
      case M_CSTYLE:   CYCLEV(opt_colorcorr, 3); preview_later(true); break;
      case M_SAT:      STEP(opt_sat, 50, 150, 5); preview_later(true); break;
      case M_BRIGHT:   STEP(opt_bright, 70, 130, 5); preview_later(true); break;
      case M_3D:       d3.on ^= 1; preview_later(false);
                       if (d3.on && !video_thread_enabled) return "3D needs Draw on core 2 On";
                       break;
      case M_3DPCT:    STEP(d3.pct, 0, 200, 10); preview_later(false); break;
      case M_3DMETHOD: d3.fast ^= 1; preview_later(false); break;
      case M_3DSMART:  d3.smart ^= 1; preview_later(false); break;
      case M_3DSTYLE:
         CYCLEV(d3.style, D3_STYLES);
         d3_set_style(d3.style);
         preview_later(false);
         break;
      case M_L0: case M_L1: case M_L2: case M_L3:
         STEP(d3.layer[id - M_L0], -8, 8, 1);
         d3.style = D3_STYLES - 1;   /* now Custom */
         preview_later(false);
         break;
      case M_SPRITE:   STEP(d3.sprite, -4, 4, 1); d3.style = D3_STYLES - 1; preview_later(false); break;
      case M_FOCUS:    STEP(d3.focus, -6, 6, 1); preview_later(false); break;
      case M_TILT:     STEP(d3.tilt, -8, 8, 1); preview_later(false); break;
      case M_GAPFILL:  opt_gapfill ^= 1; video_thread_set_gap_fill(opt_gapfill); preview_later(false); break;
      case M_PERGAME:
         if (d3_pergame)
         {
            /* back to the all-games settings; forget this game's profile */
            char p[600];
            profile_path(p, sizeof(p));
            remove(p);
            d3 = d3_global;
            d3_pergame = false;
            preview_later(false);
            return NULL;
         }
         d3_global = d3;      /* the current values are the all-games ones too */
         d3_pergame = true;
         profile_save();
         break;
      case M_SKIP:     opt_frameskip ^= 1; break;
      case M_ASTRETCH: opt_audio_stretch ^= 1; break;
      case M_FFMODE:   opt_ff_toggle ^= 1; ff_on = false; break;
      case M_FFSPEED:  CYCLEV(opt_ff_speed, 4); break;
      case M_RTC:      CYCLEV(opt_rtc, 3); break;
      case M_INTRO:    opt_intro ^= 1; break;
      case M_BOTTOM:   CYCLEV(opt_bottom, 3); break;
      case M_STATS:    opt_profiler ^= 1; vars_changed = true; break;
      case M_CORE2:
         opt_core2 ^= 1;
         core2_apply();
         stereo_apply();
         if (opt_core2 != video_thread_enabled)
            return "Core 2 could not be started";
         break;
      case M_GPU:
         opt_gpu ^= 1;
         if (opt_gpu && !gpu_ok)
         {
            gpu_guard_set("starting the GPU (menu)");
            gpu_ok = gpu_init();
            if (gpu_ok)
            {
               gpu_irq_hook();
               present_start();
            }
            gpu_guard_clear();
            if (!gpu_ok)
               return "GPU could not be started";
         }
         gpu_guard_set("clearing the screen (menu)");
         preview_refresh();
         gpu_guard_clear();
         if (GPU_ACTIVE)
            gpu_guard_set("the first frames after turning GPU display On");
         break;
      case M_PRIO:
         opt_lowprio ^= 1;
         video_thread_set_low_priority(opt_lowprio);
         break;
   }
   return NULL;
#undef STEP
#undef CYCLEV
}

/* returns false to leave the game */
static bool game_menu(void)
{
   static int tab, sel_of_tab[MENU_NTABS] = { 0, 0, 0, 0, 0 }, scroll_of_tab[MENU_NTABS];
   static ui_row rows[MENU_MAXROWS];
   char note[64] = "";
   int note_ok = 1, i;
   bool redraw = true, touch_down = true;   /* the touch that opened the menu */
   int drag_row = -1, touch_y0 = 0, touch_moved = 0;
   float last_slider = osGet3DSliderState();
   ui_menu m;

   gpu_guard_clear();   /* got this far: the game was not frozen */
   if (audio_ok)
      ndspChnSetPaused(0, true);
   sram_flush(false);
   log_flush();
   confirm_id = 0;

   while (aptMainLoop())
   {
      u32 down, held;
      int sel = sel_of_tab[tab], act_id = 0, act_dir = 0, act_has_v = 0, act_v = 0;
      bool activate = false;

      menu_build(tab);
      if (sel >= mrow_n) sel = mrow_n - 1;
      while (sel > 0 && mrows[sel].r.kind == ROW_HEADER) sel--;

      hidScanInput();
      down = hidKeysDown() | (hidKeysDownRepeat() & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT));
      held = hidKeysHeld();

      if (down & KEY_B)
         break;
      if (down & (KEY_L | KEY_R))
      {
         tab = (tab + ((down & KEY_L) ? MENU_NTABS - 1 : 1)) % MENU_NTABS;
         note[0] = 0; confirm_id = 0; redraw = true;
         continue;
      }
      if (down & (KEY_DOWN | KEY_UP))
      {
         int d = (down & KEY_DOWN) ? 1 : -1, n = sel;
         do n = (n + d + mrow_n) % mrow_n; while (mrows[n].r.kind == ROW_HEADER || mrows[n].r.kind == ROW_INFO);
         sel = n;
         note[0] = 0; confirm_id = 0; redraw = true;
      }
      if (down & (KEY_LEFT | KEY_RIGHT))
      {
         act_id = mrows[sel].id;
         act_dir = (down & KEY_LEFT) ? -1 : 1;
      }
      if (down & KEY_A)
         activate = true;

      /* build the view for hit testing / drawing */
      for (i = 0; i < mrow_n; i++)
         rows[i] = mrows[i].r;
      {
         int sc = scroll_of_tab[tab];
         if (sel < sc) sc = sel;
         if (sel >= sc + MENU_ROWS_VIS) sc = sel - MENU_ROWS_VIS + 1;
         if (sc > mrow_n - MENU_ROWS_VIS) sc = mrow_n - MENU_ROWS_VIS;
         if (sc < 0) sc = 0;
         scroll_of_tab[tab] = sc;
      }
      m.tabs = menu_tabs; m.ntabs = MENU_NTABS; m.tab = tab;
      m.rows = rows; m.nrows = mrow_n; m.sel = sel; m.scroll = scroll_of_tab[tab];

      /* touch */
      if (held & KEY_TOUCH)
      {
         touchPosition tp;
         hidTouchRead(&tp);
         if (!touch_down)
         {
            ui_hit h = scr_menu_hit(&m, tp.px, tp.py);
            touch_down = true;
            touch_y0 = tp.py;
            touch_moved = 0;
            drag_row = -1;
            if (h.what == HIT_TAB && h.tab != tab)
            {
               sel_of_tab[tab] = sel;
               tab = h.tab;
               note[0] = 0; confirm_id = 0; redraw = true;
               continue;
            }
            if (h.what == HIT_ROW)
            {
               const ui_row *r = &rows[h.row];
               if (h.row != sel)
               {
                  sel = h.row;
                  note[0] = 0;
                  if (confirm_id != mrows[sel].id) confirm_id = 0;
               }
               redraw = true;
               if (!r->disabled || mrows[sel].id == M_3D)
               {
                  switch (h.zone)
                  {
                     case ZONE_DEC:    act_id = mrows[sel].id; act_dir = -1; break;
                     case ZONE_INC:    act_id = mrows[sel].id; act_dir = 1; break;
                     case ZONE_TOGGLE: act_id = mrows[sel].id; act_dir = 1; break;
                     case ZONE_SLIDER: act_id = mrows[sel].id; act_has_v = 1; act_v = h.slider_v; drag_row = sel; break;
                     default:
                        if (r->kind == ROW_ACTION) activate = true;
                        else if (r->kind == ROW_CHOICE) { act_id = mrows[sel].id; act_dir = 1; }
                        break;
                  }
               }
            }
         }
         else
         {
            if (abs(tp.py - touch_y0) > 10)
               touch_moved = 1;
            if (drag_row >= 0 && drag_row == sel)
            {
               /* dragging a slider */
               int span = rows[sel].vmax - rows[sel].vmin;
               int x0 = 150, x1 = UI_W - 64, xx = tp.px < x0 ? x0 : tp.px > x1 ? x1 : tp.px;
               int v = rows[sel].vmin + ((xx - x0) * span + (x1 - x0) / 2) / (x1 - x0);
               if (v != rows[sel].v)
               {
                  act_id = mrows[sel].id; act_has_v = 1; act_v = v;
               }
            }
            else if (touch_moved && drag_row < 0)
            {
               /* drag the list */
               int d = (touch_y0 - tp.py) / MENU_ROW_H;
               if (d)
               {
                  int sc = scroll_of_tab[tab] + d;
                  if (sc > mrow_n - MENU_ROWS_VIS) sc = mrow_n - MENU_ROWS_VIS;
                  if (sc < 0) sc = 0;
                  scroll_of_tab[tab] = sc;
                  if (sel < sc) sel = sc;
                  if (sel >= sc + MENU_ROWS_VIS) sel = sc + MENU_ROWS_VIS - 1;
                  touch_y0 = tp.py;
                  redraw = true;
               }
            }
         }
      }
      else
      {
         touch_down = false;
         drag_row = -1;
      }

      sel_of_tab[tab] = sel;

      if (activate && !act_id)
      {
         const mrow *r = &mrows[sel];
         if (r->r.kind == ROW_ACTION && !r->r.disabled)
         {
            note[0] = 0; note_ok = 1; redraw = true;
            switch (r->id)
            {
               case M_RESUME:
                  goto resume;
               case M_GUIDE:
                  guide_shown = !guide_shown;
                  guide_save_pos();
                  goto resume;
               case M_SAVE:
                  if (state_save())
                     snprintf(note, sizeof(note), "Saved to slot %d", opt_slot);
                  else
                  {
                     snprintf(note, sizeof(note), "Could not save to slot %d", opt_slot);
                     note_ok = 0;
                  }
                  break;
               case M_LOAD:
                  if (state_load())
                  {
                     config_save();
                     goto resume;
                  }
                  snprintf(note, sizeof(note), "Could not load slot %d", opt_slot);
                  note_ok = 0;
                  break;
               case M_RESET:
               case M_QUIT:
                  if (confirm_id != r->id)
                  {
                     confirm_id = r->id;
                     snprintf(note, sizeof(note), "Press A / tap again to %s", r->id == M_RESET ? "reset" : "quit");
                     note_ok = 0;
                     break;
                  }
                  if (r->id == M_RESET)
                  {
                     retro_reset();
                     goto resume;
                  }
                  config_save();
                  quit_app = true;
                  if (audio_ok)
                     ndspChnSetPaused(0, false);
                  return false;
               case M_LIST:
                  config_save();
                  if (audio_ok)
                     ndspChnSetPaused(0, false);
                  return false;
               case M_C2TEST:
                  scr_message("Core 2 speed test", "Measuring for 3 seconds...", NULL);
                  ui_present();
                  core2_test(core2_result, sizeof(core2_result));
                  snprintf(note, sizeof(note), "limit -> speed: %.40s", core2_result);
                  break;
               case M_COLRESET:
                  opt_colorcorr = 0; opt_sat = 100; opt_bright = 100;
                  preview_later(true);
                  snprintf(note, sizeof(note), "Colours back to Original");
                  break;
               case M_3DRESET:
                  d3.pct = 100; d3.style = 0; d3.sprite = 0; d3.focus = 0; d3.smart = 1; d3.fast = 0; d3.tilt = 0;
                  d3_set_style(0);
                  preview_later(false);
                  snprintf(note, sizeof(note), "3D back to Classic, 100%%");
                  break;
            }
         }
         else if (r->r.kind == ROW_TOGGLE || r->r.kind == ROW_CHOICE)
         {
            act_id = r->id;
            act_dir = 1;
         }
      }

      if (act_id && (!mrows[sel].r.disabled || act_id == M_3D))
      {
         const char *msg = menu_change(act_id, act_dir, act_has_v != 0, act_v);
         note[0] = 0; note_ok = 1; confirm_id = 0;
         if (msg)
         {
            snprintf(note, sizeof(note), "%s", msg);
            note_ok = 0;
         }
         /* (the game's 3D profile is written once, when the menu closes:
          * a file write per press made the menu stutter) */
         redraw = true;
      }

      /* the 3D slider moved: show it */
      {
         float sl = osGet3DSliderState();
         if (fabsf(sl - last_slider) > 0.03f)
         {
            last_slider = sl;
            if (tab == 2 || d3.on)
               preview_later(false);
         }
      }

      if (redraw)
      {
         menu_build(tab);
         for (i = 0; i < mrow_n; i++)
            rows[i] = mrows[i].r;
         m.rows = rows; m.nrows = mrow_n; m.sel = sel_of_tab[tab];
         m.scroll = scroll_of_tab[tab];
         m.help = mrows[m.sel].help;
         m.note = note[0] ? note : NULL;
         m.note_ok = note_ok;
         scr_menu(&m);
         ui_present();
         redraw = false;
      }
      preview_tick();
      gspWaitForVBlank();
   }
resume:
   if (preview_cc)   /* colours changed just before closing the menu */
   {
      cc_rebuild();
      preview_cc = false;
   }
   preview_wait = 0;
   config_save();
   profile_save();
   if (audio_ok)
      ndspChnSetPaused(0, false);
   return !quit_app && aptMainLoop();
}

/* ------------------------------------------------------------------ */
/* Frame skip (Auto): when core 2 can't draw a frame within one frame's */
/* time, draw every other frame. The skipped frame lets core 2 finish   */
/* the drawn one while the game keeps running at full speed.           */
/* ------------------------------------------------------------------ */

static float work_ms_avg = 16.0f;   /* main loop time per frame (smoothed) */
static float emu_ms_avg = 12.0f;    /* the same without waiting for core 2 */
static int   heavy_frames;
static bool skip_heavy;      /* core 2 too slow for every frame */
static int  skip_fits;       /* drawn pictures, for skip_hist */
static u8   skip_hist[8];    /* frames skipped needed by the last 8 drawn pictures */
static int  skip_phase;      /* heavy mode: 0 = draw (deferred), >0 = skips left */
static int  skip_count;
static u32  frames_drawn;

/* decide for the coming frame; returns true if it will be drawn */
static bool frame_skip_plan(void)
{
   if (opt_frameskip && video_thread_enabled)
   {
      /* too slow if core 2 can't draw a frame in a frame's time, or the
       * whole frame (emulation + showing it) takes longer than 1/60 s */
      u32 c2 = video_thread_last_render_us() + video_thread_last_hook_us();
      if (!skip_heavy && (c2 > 15500 || work_ms_avg > 16.9f))
      {
         skip_heavy = true;
         heavy_frames = 0;
      }
      /* leave only when a full-rate frame clearly fits on both cores, and
       * not before 2 seconds, so it doesn't flap on and off */
      else if (skip_heavy && ++heavy_frames > 120 && c2 < 13500 && emu_ms_avg < 13.5f)
         skip_heavy = false;
   }
   else
      skip_heavy = false;

   if (!skip_heavy)
   {
      skip_phase = 0;
      skip_count = 0;
      memset(skip_hist, 0, sizeof(skip_hist));
      return true;
   }
   if (skip_phase == 0)
   {
      /* draw this one, then skip as many frames as core 2 needs to finish
       * it (heavy 3D screens can take 50-90 ms on an Old 3DS): the game
       * never waits for core 2, so it and the sound stay at full speed */
      u32 c2 = video_thread_last_render_us() + video_thread_last_hook_us();
      int need = (int)((c2 + 14999) / 15000) - 1;
      if (need < 1) need = 1;
      if (need > 5) need = 5;
      /* Keep the rhythm steady: use the most any of the last 8 pictures
       * needed, so one picture right at the edge doesn't make it alternate
       * 1-of-2 / 1-of-3 (small hitches), yet it settles back within about
       * a second after a heavy screen. */
      {
         int k, most = need;
         skip_hist[skip_fits++ & 7] = (u8)need;
         for (k = 0; k < 8; k++)
            if (skip_hist[k] > most)
               most = skip_hist[k];
         skip_count = most;
      }
      video_thread_defer_next_finish();
      skip_phase = skip_count;
      return true;
   }
   gpsp_frontend_skip = 1;
   skip_phase--;
   return false;
}

/* a frame whose finish was deferred is done: show it */
static void frame_present_deferred(void)
{
   if (!video_thread_complete_deferred())
      return;
   if (hook_presented)
   {
      hook_presented = false;
      gpu_request_present();
   }
   else if (hook_blitted)
   {
      hook_blitted = false;
      frame_ready = true;
   }
}

/* ------------------------------------------------------------------ */
/* Audio when slow (Stretch): play the sound slower instead of letting  */
/* it run dry, matched to how fast the game can actually run.          */
/* ------------------------------------------------------------------ */

static float audio_rate_cur = AUDIO_RATE;

static float audio_q_avg = AUDIO_TARGET;

/* Called once per frame. Normally the game is paced by the sound, so the
 * queue stays near AUDIO_TARGET and the rate is untouched. If the game
 * can't keep up, the queue runs low: the sound is then slowed to the speed
 * the game actually manages, smoothly, so it never runs dry (crackle). */
static void audio_stretch_update(float work_ms)
{
   float rate;
   work_ms_avg += (work_ms - work_ms_avg) * 0.08f;
   if (!audio_ok)
      return;
   audio_q_avg += ((float)audio_queued() - audio_q_avg) * 0.02f;
   if (!opt_audio_stretch || audio_q_avg >= 3.0f)
      rate = AUDIO_RATE;
   else
   {
      /* 3 buffers queued -> full speed, 1.5 -> 60% (balance point found
       * automatically: a lower rate lets the queue grow again) */
      float f = 0.6f + (audio_q_avg - 1.5f) * (0.4f / 1.5f);
      if (f < 0.6f) f = 0.6f;
      if (f > 1.0f) f = 1.0f;
      rate = AUDIO_RATE * f;
   }
   if (fabsf(rate - audio_rate_cur) > AUDIO_RATE * 0.01f || (rate == AUDIO_RATE && audio_rate_cur != AUDIO_RATE))
   {
      audio_rate_cur = rate;
      ndspChnSetRate(0, rate);
   }
}

static void run_game(void)
{
   u64 t_frame0 = svcGetSystemTick(), t_win0 = t_frame0;
   u64 t_core = 0, t_wait = 0, t_rest = 0;
   u64 c2_draw_us = 0, c2_copy_us = 0, c2_wait_us = 0;
   u32 prev_underruns = audio_underruns, prev_drops = audio_drops;
   (void)prev_drops;
   u32 frames = 0, frames_total = 0, drawn_win = 0, ff_extra_win = 0;
   bool ff_shown = false, hud_now = false, open_menu, guide_dirty = false, eat_touch = false;

   {
      ui_hud h;
      memset(&h, 0, sizeof(h));
      h.title = rom_name;
      h.fps = 60;
      h.pictures = 60;
      if (guide_shown)
      {
         guide_invalidate();
         guide_draw();
      }
      else
         scr_hud(&h);
      ui_present();
   }
   hud_have = false;
   bottom_asleep = false;
   bottom_idle_t0 = svcGetSystemTick();
   stereo_apply();
   if (GPU_ACTIVE && video_thread_enabled)
      gpu_guard_set("the first frames of a game");
   clear_top();
   skip_heavy = false;
   skip_phase = 0;
   work_ms_avg = 16.0f;
   audio_q_avg = AUDIO_TARGET;

   while (aptMainLoop())
   {
      u64 t0, t1, t2;
      bool drawn;
      hidScanInput();
      keys_held = hidKeysHeld();
      gyro_update();
      open_menu = (hidKeysDown() & KEY_TOUCH) != 0;
      if (keys_held & KEY_TOUCH)
      {
         if (bottom_asleep)
         {
            bottom_wake();
            eat_touch = true;   /* the tap only wakes the bottom screen */
         }
         bottom_idle_t0 = svcGetSystemTick();
      }
      else
         eat_touch = false;
      if (eat_touch)
         open_menu = false;
      else if (guide_shown)
      {
         /* the guide takes the touches: drag to scroll, Menu opens the menu */
         touchPosition tp;
         int r = 0;
         if (hidKeysDown() & KEY_TOUCH)
         {
            hidTouchRead(&tp);
            r = guide_touch(GUIDE_TOUCH_DOWN, tp.px, tp.py);
         }
         else if (keys_held & KEY_TOUCH)
         {
            hidTouchRead(&tp);
            r = guide_touch(GUIDE_TOUCH_HELD, tp.px, tp.py);
         }
         else if (hidKeysUp() & KEY_TOUCH)
            r = guide_touch(GUIDE_TOUCH_UP, 0, 0);
         else
            r = guide_tick();   /* a flick keeps scrolling for a moment */
         open_menu = r == GUIDE_MENU;
         if (r == GUIDE_REDRAW)
            guide_dirty = true;
      }
      if (open_menu)
      {
         frame_present_deferred();
         guide_save_pos();
         if (!game_menu())
            return;
         guide_invalidate();   /* the menu drew over the canvas */
         guide_dirty = guide_shown;
         bottom_idle_t0 = svcGetSystemTick();
         clear_top();
         skip_phase = 0;
         t_frame0 = t_win0 = svcGetSystemTick();
         t_core = t_wait = t_rest = 0;
         c2_draw_us = c2_copy_us = c2_wait_us = 0;
         t_blit_acc = 0;
         frames = drawn_win = 0;
         continue;
      }

      t0 = svcGetSystemTick();
      frame_ready = false;
      /* not while core 2 may still be drawing a deferred frame */
      if (!(skip_heavy && skip_phase > 0))
      {
         if (stereo_mode_check())
            clear_top();   /* the other mode's framebuffer layout: start clean */
         stereo_update();
      }
      drawn = frame_skip_plan();
      retro_run();
      if (skip_phase == 0)   /* after the last skipped frame: show the drawn one */
         frame_present_deferred();
      t1 = svcGetSystemTick();

      audio_submit();
      if (frame_ready)
      {
         flush_gpu(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), TOP_FB_BYTES);
         if (stereo_mode)
            flush_gpu(gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), TOP_FB_BYTES);
         gfxScreenSwapBuffers(GFX_TOP, stereo_mode);
      }
      t2 = svcGetSystemTick();
      audio_stretch_update((float)((t2 - t0) / TICKS_PER_MS));
      {
         float emu = (float)((t1 - t0) / TICKS_PER_MS) -
               (video_thread_enabled ? video_thread_last_wait_us() / 1000.0f : 0.0f);
         emu_ms_avg += (emu - emu_ms_avg) * 0.05f;
      }

      /* Fast forward (Y): run extra frames without drawing them or playing
       * their sound. They come after this frame's timing is measured, so
       * frame skip and audio stretch see a normal frame. The loop is still
       * paced by the sound, so N-1 extra frames make it N times as fast; at
       * Max it fills the frame's time with as many as fit. */
      {
         bool ff;
         if (opt_ff_toggle)
         {
            if (hidKeysDown() & KEY_Y)
               ff_on = !ff_on;
            ff = ff_on;
         }
         else
            ff = (keys_held & KEY_Y) != 0;
         if (ff)
         {
            int extra = 0, want = opt_ff_speed ? opt_ff_speed : 15;
            u64 budget = t0 + (u64)(TICKS_PER_MS * 15.5);
            while (extra < want && (opt_ff_speed || svcGetSystemTick() < budget))
            {
               gpsp_frontend_skip = 1;
               retro_run();
               pending_frames = 0;   /* their sound is dropped */
               extra++;
            }
            ff_extra_win += extra;
         }
         if (ff != ff_shown)
         {
            ff_shown = ff;
            hud_now = true;   /* show it on the bottom screen right away */
         }
      }

      /* Pace to the audio: keep a few buffers queued. When the game can't
       * keep up the queue is already short and we don't wait at all. */
      if (audio_ok)
      {
         /* At most 3 frames: when the lid closes the system stops the DSP
          * before we get to aptMainLoop(), so the queue never drains, and
          * an endless wait here kept the 3DS from going to sleep (freeze). */
         u64 give_up = svcGetSystemTick() + (u64)(TICKS_PER_MS * 50);
         while (audio_queued() > AUDIO_TARGET && svcGetSystemTick() < give_up)
            svcSleepThread(500000);
      }
      else
      {
         u64 period = (u64)(SYSCLOCK_ARM11 * 280896.0 / 16777216.0);
         u64 now = svcGetSystemTick();
         t_frame0 += period;
         if (now < t_frame0)
            svcSleepThread((s64)((t_frame0 - now) * 1000000000.0 / SYSCLOCK_ARM11));
         else if (now - t_frame0 > 4 * period)
            t_frame0 = now;
      }

      t_core += t1 - t0;
      t_rest += t2 - t1;
      if (drawn)
      {
         drawn_win++;
         frames_drawn++;
         if (video_thread_enabled)
         {
            c2_draw_us += video_thread_last_render_us();
            c2_copy_us += video_thread_last_hook_us();
         }
      }
      if (video_thread_enabled)
         c2_wait_us += video_thread_last_wait_us();
      t_wait += svcGetSystemTick() - t2;
      frames++;
      frames_total++;

      if ((frames_total & 127) == 0)
         sram_flush(false);
      if (frames_total == 180)
         gpu_guard_clear();   /* 3 seconds of frames: the GPU display works */

      bottom_check_idle();

      if (guide_dirty)
      {
         guide_draw();
         ui_present();
         guide_dirty = false;
      }

      if (frames == 60 || hud_now)
      {
         u64 now  = svcGetSystemTick();
         double ms = (now - t_win0) / TICKS_PER_MS;
         double fps = frames * 1000.0 / ms;
         u32 dw = drawn_win ? drawn_win : 1;
         {
            static ui_hud h;
            memset(&h, 0, sizeof(h));
            h.title = rom_name;
            h.fps = (float)fps;
            h.pictures = (float)(fps * drawn_win / (frames ? frames : 1));
            h.stereo = stereo_mode && stereo_amount_pct() > 0;
            h.depth_pct = stereo_amount_pct();
            h.show_stats = opt_profiler;
            h.warning = audio_ok ? NULL : "No sound: dspfirm.cdc is missing";
            if (ff_shown)
            {
               static char ffw[48];
               snprintf(ffw, sizeof(ffw), ">> Fast forward  %.1fx",
                        frames ? (frames + ff_extra_win) / (double)frames : 1.0);
               h.warning = ffw;
            }
            h.stat_label[0] = "Main core";
            snprintf(h.stat_value[0], 40, "emulate %.1f ms   show %.1f ms",
                  (t_core - t_blit_acc) / TICKS_PER_MS / frames - c2_wait_us / 1000.0 / frames,
                  (t_blit_acc / TICKS_PER_MS + present_us_acc / 1000.0) / dw);
            h.stat_label[1] = "Waits";
            snprintf(h.stat_value[1], 40, "%.1f ms for core 2", c2_wait_us / 1000.0 / frames);
            h.stat_label[2] = "Core 2";
            if (video_thread_enabled)
               snprintf(h.stat_value[2], 40, "draw %.1f ms   copy %.1f ms",
                     c2_draw_us / 1000.0 / dw, c2_copy_us / 1000.0 / dw);
            else
               snprintf(h.stat_value[2], 40, "off (drawing on main core)");
            h.stat_label[3] = "Display";
            if (skip_heavy)
               snprintf(h.stat_value[3], 40, "%s, drawing 1 of %d frames", GPU_ACTIVE ? "GPU" : "CPU", skip_count + 1);
            else
               snprintf(h.stat_value[3], 40, "%s, every frame", GPU_ACTIVE ? "GPU" : "CPU copy");
            h.stat_label[4] = "Audio";
            snprintf(h.stat_value[4], 40, "gaps %lu/s   speed %.0f%%",
                  (unsigned long)(audio_underruns - prev_underruns), audio_rate_cur * 100.0 / AUDIO_RATE);
            h.stat_label[5] = "Emulator";
            snprintf(h.stat_value[5], 40, "%.38s", core_msg);
            h.nstats = core_msg[0] ? 6 : 5;
            hud_last = h;
            hud_have = true;
            hud_show();
         }
         prev_underruns = audio_underruns;
         prev_drops     = audio_drops;
         audio_min_queue = 99;
         t_win0 = now;
         t_core = t_wait = t_rest = 0;
         c2_draw_us = c2_copy_us = c2_wait_us = 0;
         t_blit_acc = 0;
         present_us_acc = 0;
         frames = drawn_win = 0;
         ff_extra_win = 0;
         hud_now = false;
      }
   }
}

/* Started with a game (forwarders / HOME Menu shortcuts pass the ROM path
 * as the first argument): play it, and close the app when it ends. */
static bool direct_launch, direct_pending;
static void direct_launch_init(int argc, char **argv)
{
   const char *a = argc > 1 && argv ? argv[1] : NULL;
   static char from_file[512];
   if (!a || !*a)
   {
      /* HOME Menu forwarders (made by make-forwarders.sh) leave the game's
       * path here and then start gpSP 3D */
      FILE *f = fopen(LAUNCH_PATH, "r");
      if (!f)
         return;
      from_file[0] = 0;
      if (fgets(from_file, sizeof(from_file), f))
         from_file[strcspn(from_file, "\r\n")] = 0;
      fclose(f);
      remove(LAUNCH_PATH);   /* one launch per request */
      a = from_file;
      if (!*a)
         return;
   }
   if (a[0] == '/')
      snprintf(rom_path, sizeof(rom_path), "sdmc:%s", a);
   else
      snprintf(rom_path, sizeof(rom_path), "%s", a);
   if (!file_exists(rom_path))
   {
      rom_path[0] = 0;
      return;
   }
   direct_launch = direct_pending = true;
}
static bool next_game(void)
{
   if (direct_pending)
   {
      direct_pending = false;
      return true;
   }
   if (direct_launch)
      return false;
   return browse();
}

int main(int argc, char **argv)
{
   aptHookCookie cookie;
   bool dynarec;

   gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
   gfxSetDoubleBuffering(GFX_TOP, true);
   gfxSetDoubleBuffering(GFX_BOTTOM, true);
   osSetSpeedupEnable(true);
   {
      /* the Old 2DS has no 800px mode */
      u8 model = 0;
      if (R_SUCCEEDED(cfguInit()))
      {
         if (R_SUCCEEDED(CFGU_GetSystemModel(&model)) && model == CFG_MODEL_2DS)
            wide_allowed = false;
         cfguExit();
      }
   }
   hidSetRepeatParameters(15, 4);   /* first repeat after 1/4 s, then 15 per second */

   mkdir("sdmc:/3ds", 0777);
   mkdir(APP_DIR, 0777);
   config_load();
   if (!dir_exists(cur_dir))
      strcpy(cur_dir, "sdmc:/");

   /* Kernel access for the dynarec (needs Luma3DS / Rosalina). */
   dynarec = R_SUCCEEDED(svchax_init(false)) && __ctr_svchax;
   fast_flush = dynarec;

   audio_init();
   aptHook(&cookie, apt_hook, NULL);

   /* Our own buffers first: gpSP grabs most of the free heap for ROM blocks. */
   retro_set_environment(environment);
   retro_set_video_refresh(video_refresh);
   retro_set_audio_sample(audio_sample);
   retro_set_audio_sample_batch(audio_batch);
   retro_set_input_poll(input_poll);
   retro_set_input_state(input_state);
   state_size = retro_serialize_size();
   state_buf  = (u8*)malloc(state_size);
   cc_rebuild();
   {
      const char *froze = gpu_guard_check();
      if (froze)
      {
         bool closing = !strncmp(froze, "closing", 7);
         if (!closing)
         {
            opt_gpu = 0;
            config_save();
         }
         char body[200];
         snprintf(body, sizeof(body), "During: %s\n\n%s", froze, closing ? "" :
                "GPU display is now Off (menu: System >\nGPU display). The CPU shows the frames.");
         ui_wait_message("The 3DS froze last time", body);
      }
   }
   if (opt_gpu)
   {
      gpu_guard_set("starting the GPU");
      gpu_ok = gpu_init();
      if (gpu_ok)
      {
         gpu_irq_hook();
         present_start();
      }
   }
   core2_apply();   /* start drawing on core 2 (if enabled); before gpSP takes the heap */
   video_thread_set_gap_fill(opt_gapfill);
   stereo_apply();
   retro_init();

   if (!dynarec)
   {
      ui_wait_message("Running without the dynarec",
            "Fast emulation needs Luma3DS (kernel\naccess). Games will run slowly.");
   }

   if (gpu_ok)
      gpu_guard_set("clearing the top screen with the GPU");
   clear_top();
   gpu_guard_clear();
   direct_launch_init(argc, argv);
   while (!quit_app && next_game())
   {
      {
         const char *nm = strrchr(rom_path, '/');
         scr_message("Loading", nm ? nm + 1 : rom_path, NULL);
         ui_present();
      }
      if (!game_load())
      {
         ui_wait_message("Could not load this game", load_err[0] ? load_err :
                         "The file may be damaged or not a GBA\ngame.");
         continue;
      }
      run_game();
      game_unload();
      if (!aptMainLoop())
         break;   /* closing: no more screen work */
      clear_top();
   }

   /* No retro_deinit(): unmapping the dynarec cache is what we want to avoid on
    * the way out; the system reclaims everything when the app exits. */
   gpu_guard_set("closing: saving");
   game_unload();
   config_save();
   /* Shut down in order so the system can close us: stop the core-2 thread
    * first, then let the GPU finish and release it, then audio and screens. */
   gpu_guard_set("closing: stopping core 2");
   if (video_thread_enabled)
      video_thread_stop();
   gpu_guard_set("closing: releasing the GPU");
   present_stop();
   if (gpu_ok)
   {
      gpu_unstick(SYSCLOCK_ARM11 / 10);
      if (gxCmdQueueWait(NULL, 0))
      {
         aptUnhook(&gpu_apt_cookie);
         C3D_Fini();
      }
      gpu_ok = false;
   }
   aptUnhook(&cookie);
   gpu_guard_set("closing: stopping audio");
   if (audio_ok)
      ndspExit();
   gpu_guard_set("closing: releasing the screens");
   if (bottom_lcd_off)
      lcd_backlight(true);
   gfxExit();
   gpu_guard_clear();
   return 0;
}

static int opt_gpu_fwd(void) { return opt_gpu; }
static int opt_smooth_fwd(void) { return opt_smooth; }
static void profile_load_fwd(void) { profile_load(); }
static void profile_leave_fwd(void) { profile_leave(); }
