/* Deferred scanline rendering on a second CPU core.
 *
 * The emulation thread records, per visible scanline, everything gpSP's
 * renderer reads (I/O registers, affine reference points, and the palette and
 * OAM whenever they changed), plus a copy of VRAM at the start of each frame.
 * A render thread runs gpSP's own renderer on those copies, a few lines behind
 * the emulation, so the picture is identical to the single-threaded one and
 * no extra frame of latency is added.
 */
#ifndef VIDEO_THREAD_H
#define VIDEO_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Set while the render thread is running; update_scanline() checks it. */
extern volatile int video_thread_enabled;

/* Start the render thread on the given CPU core (-1 = any). Returns 0 on
 * success. Must be called after retro_init() and before running frames. */
int video_thread_start(int core);
void video_thread_stop(void);

/* Emulation side (called by update_scanline for lines 0..159). */
void video_thread_record_line(unsigned vcount);

/* Waits until the frame recorded during the last retro_run() is fully drawn
 * and returns its pixels (GBA_SCREEN_PITCH stride), or NULL if no frame was
 * recorded (frame skipped). */
const unsigned short *video_thread_finish_frame(void);

/* Microseconds the render thread spent drawing during the last finished
 * frame, and how long the emulation thread waited for it. */
unsigned video_thread_last_render_us(void);
unsigned video_thread_last_hook_us(void);

/* Optional callback run on the render thread for every finished frame, before
 * video_thread_finish_frame() returns on the emulation thread. NULL = none. */
void video_thread_set_frame_hook(void (*hook)(const unsigned short *left, const unsigned short *right, unsigned pitch, void *arg), void *arg);

/* Colour correction done on the palette (cheap) instead of per pixel; lut is
 * indexed by RGB555 like gba_cc_lut. NULL = off. Direct-colour bitmap modes
 * (3 and 5) are not corrected. Call between frames. */
void video_thread_set_color_correction(const unsigned short *lut);

/* Stereoscopic 3D. depth[p] = depth in GBA pixels of everything with BG/OBJ
 * priority p (positive = toward the viewer); NULL = mono. Each line is then
 * drawn twice and the frame hook gets a right-eye image too. Call between
 * frames (e.g. before retro_run()). */
void video_thread_set_stereo(const int depth[4]);
/* Same, with a separate depth for sprites of each priority. */
void video_thread_set_stereo2(const int bg_depth[4], const int obj_depth[4]);
/* Draw the last finished frame again with the current settings and run the
 * frame hook on it (game paused, between frames). Returns 1 if drawn. */
int video_thread_redraw(void);
/* 3D: fill the thin seams where layers at different depths slide apart
 * and the backdrop shows through (default on). */
void video_thread_set_gap_fill(int on);
/* 3D: BG layers that scroll together (one picture split over layers) share
 * one depth (default on). */
void video_thread_set_smart_layers(int on);
/* 3D method: 0 = Accurate (each eye drawn with shifted layers), 1 = Fast
 * (drawn once, pixels moved by their layer's depth, uncovered spots filled
 * from the further neighbour; about half the drawing work). */
void video_thread_set_fast3d(int on);
unsigned video_thread_last_wait_us(void);

/* Frame skipping without stalls: call before retro_run() of a frame that
 * will be drawn when the NEXT frame will be skipped. That frame's finish
 * then doesn't wait for the render thread (retro_run reports it as not
 * drawn); call video_thread_complete_deferred() after the next retro_run():
 * it waits for the frame and returns 1 (its frame hook has then run).
 * Recording a new frame completes a pending one automatically. */
void video_thread_defer_next_finish(void);
int  video_thread_complete_deferred(void);

/* Render thread priority: 1 = below system services (see video_thread_sys.h). */
void video_thread_set_low_priority(int low);

/* Totals since start: VRAM copies, and how many were mid-frame (stalls). */
void video_thread_counters(unsigned *copies, unsigned *syncs);

#ifdef __cplusplus
}
#endif

#endif
