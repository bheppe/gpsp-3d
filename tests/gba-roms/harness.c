/* gpSP test runner: harness ROM dynarec(0/1) frames [ppm] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "libretro.h"
extern unsigned int test_flush_total;   /* added to the core by run.sh */
extern unsigned int prof_ram_pages __attribute__((weak));   /* RAM code pages dropped */
extern unsigned int prof_ram_reused __attribute__((weak));  /* old translations used again */
static const char *drc = "enabled";
static uint16_t frame[160][240];
static uint32_t fhash = 2166136261u; static int nframes;
int video_thread_start(int core);
void video_thread_counters(unsigned *copies, unsigned *syncs);
static void vcb(const void *d, unsigned w, unsigned h, size_t pitch) {
  unsigned y; if (!d) return;
  for (y = 0; y < h; y++) memcpy(frame[y], (const char*)d + y * pitch, w * 2);
  { const uint8_t *b = (const uint8_t*)frame; size_t k; for (k = 0; k < sizeof(frame); k++) fhash = (fhash ^ b[k]) * 16777619u; nframes++; } }
static size_t abatch(const int16_t *d, size_t n) { return n; }
static void asample(int16_t l, int16_t r) {}
static void ipoll(void) {}
static int16_t istate(unsigned p, unsigned d, unsigned i, unsigned id) { return 0; }
static bool env(unsigned cmd, void *data) {
  switch (cmd) {
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data = "/tmp/ah"; return true;
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
  case RETRO_ENVIRONMENT_GET_VARIABLE: {
    struct retro_variable *v = data;
    if (!strcmp(v->key, "gpsp_drc")) { v->value = drc; return true; }
    return false; }
  default: return false; } }
int main(int argc, char **argv) {
  struct retro_game_info gi = { argv[1], NULL, 0, NULL };
  int frames = atoi(argv[3]), i; struct timespec a, b;
  FILE *f = fopen(argv[1], "rb"); fseek(f, 0, SEEK_END); gi.size = ftell(f); rewind(f);
  gi.data = malloc(gi.size); if (fread((void*)gi.data, 1, gi.size, f) != gi.size) return 1; fclose(f);
  drc = atoi(argv[2]) ? "enabled" : "disabled";
  retro_set_environment(env); retro_set_video_refresh(vcb); retro_set_audio_sample(asample);
  retro_set_audio_sample_batch(abatch); retro_set_input_poll(ipoll); retro_set_input_state(istate);
  retro_init();
  if (!retro_load_game(&gi)) { printf("load failed\n"); return 1; }
  if (getenv("THREAD") && *getenv("THREAD") == '1' && video_thread_start(-1) != 0) { printf("thread failed\n"); return 1; }
  clock_gettime(CLOCK_MONOTONIC, &a);
  for (i = 0; i < frames; i++) retro_run();
  clock_gettime(CLOCK_MONOTONIC, &b);
  { const uint32_t *ew = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    printf("drc=%s frames=%d flushes=%u pages=%u reused=%u time=%.0fms results:", drc, frames, test_flush_total, &prof_ram_pages ? prof_ram_pages : 0,
      &prof_ram_reused ? prof_ram_reused : 0,
      (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6);
    for (i = 0; i < 17; i++) printf(" %08X", ew ? ew[i] : 0); printf("\n");
    if (getenv("HASH")) { unsigned c = 0, sy = 0; video_thread_counters(&c, &sy); printf("frames=%d hash=%08X vram_copies=%u syncs=%u\n", nframes, fhash, c, sy); } }
  if (argc > 4) { FILE *o = fopen(argv[4], "wb"); int x, y; fprintf(o, "P6 240 160 255\n");
    for (y = 0; y < 160; y++) for (x = 0; x < 240; x++) { uint16_t c = frame[y][x];
      fputc((c >> 11) << 3, o); fputc(((c >> 5) & 63) << 2, o); fputc((c & 31) << 3, o); } fclose(o); }
  return 0; }
