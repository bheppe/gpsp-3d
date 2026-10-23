/* gpSP frame-time profiler (for finding Old 3DS bottlenecks).
 *
 * Compiled in only with -DGPSP_PROFILE. Turned on at runtime by the core
 * option "Profiler overlay". Every 60 frames it shows one line of average
 * milliseconds per frame as an on-screen message, and appends a detailed
 * report (incl. the hottest guest PCs and the code around them, for finding
 * busy-wait loops) to gpsp_profile.txt in the save directory.
 */
#ifndef GPSP_PROFILER_H
#define GPSP_PROFILER_H

#include "common.h"

#ifdef GPSP_PROFILE

typedef unsigned long long prof_t;

extern bool prof_enabled;

prof_t prof_now(void);

/* Accumulators, in ticks of prof_now() */
extern prof_t prof_video;    /* update_scanline: GBA drawing      */
extern prof_t prof_sound;    /* render_gbc_sound (sound mixing)   */
extern prof_t prof_xlate;    /* dynarec translating new blocks    */
extern u32 prof_ram_flushes; /* RAM translation cache flushes     */
extern u32 prof_rom_flushes; /* ROM translation cache flushes     */
extern u32 prof_halt_cycles; /* GBA cycles spent halted/sleeping  */

void prof_sample_pc(u32 pc, u32 cpsr);
void prof_frame_begin(void);
void prof_mark_emu_done(void);
void prof_mark_audio_done(void);
void prof_frame_end(void);
void prof_reset(void);
void prof_init(void *environ_cb);

#define PROF_BEGIN(var)   prof_t var = prof_enabled ? prof_now() : 0
#define PROF_END(var, acc) do { if (prof_enabled) acc += prof_now() - var; } while (0)

#else

#define PROF_BEGIN(var)    do {} while (0)
#define PROF_END(var, acc) do {} while (0)
#define prof_sample_pc(pc, cpsr) do {} while (0)

#endif

#endif
