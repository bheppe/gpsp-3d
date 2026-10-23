/* gpSP frame-time profiler, see profiler.h */
#ifdef GPSP_PROFILE

#include <stdio.h>
#include <string.h>
#include "common.h"
#include "libretro.h"
#include "profiler.h"

#ifdef _3DS
unsigned long long svcGetSystemTick(void);
#define PROF_TICKS_PER_MS 268111.856
prof_t prof_now(void) { return svcGetSystemTick(); }
#else
#include <time.h>
#define PROF_TICKS_PER_MS 1000000.0
prof_t prof_now(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (prof_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}
#endif

#define PROF_WINDOW      60   /* frames per on-screen update            */
#define PROF_LOG_EVERY   5    /* windows per log file entry (~5 s)      */
#define PROF_HOT_SIZE    1024 /* hot-PC hash table size (power of two)  */
#define PROF_HOT_REPORT  6
#define GBA_FRAME_CYCLES 280896

bool prof_enabled = false;

prof_t prof_video, prof_sound, prof_xlate;
u32 prof_ram_flushes, prof_rom_flushes, prof_halt_cycles;

static prof_t emu_ticks, aud_ticks, out_ticks, gap_ticks;
static prof_t t_begin, t_emu_done, t_aud_done, t_last_end;
static bool skip_gap = true;
static u32 frames, windows;

static struct { u32 key; u32 count; } hot[PROF_HOT_SIZE];
static u32 hot_total;

static retro_environment_t prof_env;
static char log_path[1024];

extern u32 function_cc read_memory16(u32 address);
extern u32 function_cc read_memory32(u32 address);

void prof_init(void *envp)
{
   retro_environment_t env = (retro_environment_t)envp;
   const char *dir = NULL;
   prof_env = env;
   log_path[0] = '\0';
   if (env && env(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir && *dir)
      snprintf(log_path, sizeof(log_path), "%s/gpsp_profile.txt", dir);
}

void prof_reset(void)
{
   prof_video = prof_sound = prof_xlate = 0;
   prof_ram_flushes = prof_rom_flushes = prof_halt_cycles = 0;
   emu_ticks = aud_ticks = out_ticks = gap_ticks = 0;
   frames = 0;
   skip_gap = true;
   memset(hot, 0, sizeof(hot));
   hot_total = 0;
}

void prof_sample_pc(u32 pc, u32 cpsr)
{
   u32 key, h, i;
   if (!prof_enabled)
      return;
   key = (pc & ~1U) | ((cpsr >> 5) & 1);   /* bit 0 = Thumb */
   h = (key * 2654435761U) >> 22;           /* 10 bits */
   for (i = 0; i < 16; i++)
   {
      u32 slot = (h + i) & (PROF_HOT_SIZE - 1);
      if (hot[slot].key == key || !hot[slot].count)
      {
         hot[slot].key = key;
         hot[slot].count++;
         hot_total++;
         return;
      }
   }
}

void prof_frame_begin(void)
{
   if (!prof_enabled)
      return;
   t_begin = prof_now();
   if (!skip_gap)
      gap_ticks += t_begin - t_last_end;
   skip_gap = false;
}

void prof_mark_emu_done(void)   { if (prof_enabled) t_emu_done = prof_now(); }
void prof_mark_audio_done(void) { if (prof_enabled) t_aud_done = prof_now(); }

static float ms(prof_t t) { return (float)(t / PROF_TICKS_PER_MS) / frames; }

static bool code_region(u32 addr)
{
   u32 r = addr >> 24;
   return r == 0x02 || r == 0x03 || (r >= 0x08 && r <= 0x0D);
}

static void write_log(float cpu, float gfx, float snd, float jit,
      float out, float aud, float wait, u32 halt_pct)
{
   FILE *f;
   u32 i, n, j;
   u32 idx[PROF_HOT_REPORT];
   u32 code = read_memory32(0x080000AC);

   if (!log_path[0] || !(f = fopen(log_path, "a")))
      return;

   if (windows == PROF_LOG_EVERY)
      fprintf(f, "=== gpSP profile, game code %.4s ===\n"
                 "ms per frame (16.7 = full speed): cpu = GBA CPU emulation (excl. gfx/snd/jit), "
                 "gfx = GBA drawing, snd = sound, jit = dynarec translation, out = video_cb, "
                 "aud = audio_cb, wait = outside the core\n", (const char*)&code);

   fprintf(f, "cpu %.2f gfx %.2f snd %.2f jit %.2f | out %.2f aud %.2f wait %.2f | "
              "halt %u%% ramflush %u romflush %u\n",
         cpu, gfx, snd, jit, out, aud, wait, halt_pct,
         prof_ram_flushes, prof_rom_flushes);

   /* top hot PCs (where update_gba was entered from = loop/branch targets) */
   for (n = 0; n < PROF_HOT_REPORT; n++)
   {
      u32 best = PROF_HOT_SIZE, bc = 0;
      for (i = 0; i < PROF_HOT_SIZE; i++)
      {
         bool used = false;
         for (j = 0; j < n; j++)
            if (idx[j] == i) used = true;
         if (!used && hot[i].count > bc) { bc = hot[i].count; best = i; }
      }
      if (best == PROF_HOT_SIZE)
         break;
      idx[n] = best;
   }
   for (i = 0; i < n; i++)
   {
      u32 key = hot[idx[i]].key, pc = key & ~1U, thumb = key & 1;
      u32 pct = hot_total ? hot[idx[i]].count * 100 / hot_total : 0;
      fprintf(f, "  hot %08X %s %u%%:", pc, thumb ? "T" : "A", pct);
      if (code_region(pc))
      {
         u32 a;
         if (thumb)
            for (a = pc - 8; a < pc + 24; a += 2)
               fprintf(f, " %04X", read_memory16(a) & 0xFFFF);
         else
            for (a = pc - 8; a < pc + 24; a += 4)
               fprintf(f, " %08X", read_memory32(a));
      }
      fprintf(f, "\n");
   }
   fclose(f);
}

void prof_frame_end(void)
{
   prof_t t_end;
   if (!prof_enabled)
      return;
   t_end = prof_now();
   emu_ticks += t_emu_done - t_begin;
   aud_ticks += t_aud_done - t_emu_done;
   out_ticks += t_end - t_aud_done;
   t_last_end = t_end;

   if (++frames >= PROF_WINDOW)
   {
      float gfx = ms(prof_video), snd = ms(prof_sound), jit = ms(prof_xlate);
      float cpu = ms(emu_ticks) - gfx - snd - jit;
      float out = ms(out_ticks), aud = ms(aud_ticks);
      float wait = frames > 1 ? (float)(gap_ticks / PROF_TICKS_PER_MS) / (frames - 1) : 0;
      u32 halt_pct = (u32)((unsigned long long)prof_halt_cycles * 100 / ((unsigned long long)GBA_FRAME_CYCLES * frames));
      char msg[128];

      snprintf(msg, sizeof(msg), "cpu%.1f gfx%.1f snd%.1f jit%.1f out%.1f aud%.1f wait%.1f halt%u%% fl%u/%u",
            cpu, gfx, snd, jit, out, aud, wait, halt_pct, prof_ram_flushes, prof_rom_flushes);
      if (prof_env)
      {
         struct retro_message m = { msg, 90 };
         prof_env(RETRO_ENVIRONMENT_SET_MESSAGE, &m);
      }

      if (++windows % PROF_LOG_EVERY == 0)
      {
         write_log(cpu, gfx, snd, jit, out, aud, wait, halt_pct);
         skip_gap = true; /* don't count the file write as frontend time */
      }

      prof_reset();
      skip_gap = windows % PROF_LOG_EVERY == 0;
   }
}

#endif
