/* Deferred scanline rendering on a second CPU core. See video_thread.h.
 *
 * gpSP's renderer (video.cc) is compiled a second time inside namespace rt.
 * Inside that namespace, the globals it reads (io_registers, vram,
 * palette_ram_converted, oam_ram, reg) resolve to the render thread's own
 * copies declared below, so the same code draws from snapshots while the
 * emulation thread keeps running.
 */

extern "C" {
  #include "common.h"
  #include "video_thread.h"
  #include "video_thread_sys.h"
}

#include <string.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Render-thread copy of the renderer                                  */
/* ------------------------------------------------------------------ */

namespace rt {
  u16  io_registers[512];
  u8  *vram;                     /* points at the current frame's snapshot */
  u16  palette_ram_converted[512];
  u16  oam_ram[512];
  u32  reg[64];
  u32  skip_next_frame;
  s32  vt_eye_obj_shift[4];      /* per-eye sprite shift by priority */

  /* Layer tracking (3D "Fast" method): which layer drew each pixel.
   * 0-3 = BG0-3, 4-7 = sprites of priority 0-3, 8 = backdrop. */
  static int rt_track;
  static u8  rt_layer_id[240];
  static u32 rt_prev[240];
  /* begin copies the segment only before its first layer (rt_seg_ptr marks
   * which buffer/segment the snapshot belongs to); end compares and keeps
   * the snapshot current, so each layer costs one pass */
  static const void *rt_seg_ptr;
  static u32 rt_seg_start;
  template<typename T> static inline void rt_track_begin(u32 s, u32 e, const T *d)
  {
    if (rt_seg_ptr == (const void*)d && rt_seg_start == s)
      return;
    rt_seg_ptr = d;
    rt_seg_start = s;
    for (u32 x = s; x < e; x++) rt_prev[x] = d[x];
  }
  template<typename T> static inline void rt_track_end(u32 s, u32 e, const T *d, int id)
  {
    for (u32 x = s; x < e; x++)
    {
      u32 v = d[x];
      if (v != rt_prev[x]) { rt_layer_id[x] = (u8)id; rt_prev[x] = v; }
    }
  }
}
#define RT_TRACK_BEGIN(s, e, d) do { if (rt_track) rt_track_begin(s, e, d); } while (0)
#define RT_TRACK_END(s, e, d, id) do { if (rt_track) rt_track_end(s, e, d, id); } while (0)

#define VIDEO_RT 1
namespace rt {
#include "video.cc"
}
#undef VIDEO_RT

/* ------------------------------------------------------------------ */
/* Snapshots                                                           */
/* ------------------------------------------------------------------ */

#define VT_LINES    GBA_SCREEN_HEIGHT
#define VT_IO_REGS  48              /* 0x00..0x5E: everything the renderer reads */
#define VT_RING     1024            /* power of two */
#define VT_MSG_SYNC 0xFFFDu
#define VT_MSG_END  0xFFFEu
#define VT_MSG_QUIT 0xFFFFu

typedef struct {
  u16 io[VT_IO_REGS];
  s32 affx[2], affy[2];
  u8  has_pal, has_oam;
  u8  vram_idx;                 /* which VRAM copy this line reads */
  u16 pal[512];
  u16 oam[512];
} vt_line;

typedef struct {
  u8      recorded[VT_LINES];   /* line was recorded this frame */
  vt_line lines[VT_LINES];
  u16     pixels[GBA_SCREEN_PITCH * (GBA_SCREEN_HEIGHT + 1)];     /* left eye / mono */
  u16     pixels_r[GBA_SCREEN_PITCH * (GBA_SCREEN_HEIGHT + 1)];   /* right eye (3D) */
} vt_frame;

/* Stereoscopic 3D: depth per BG/OBJ priority in GBA pixels (positive = toward
 * the viewer). Only changed between frames, while the render thread is idle. */
static volatile int vt_stereo;

/* Optional colour correction applied to the palette (512 entries) instead of
 * every pixel: lut is indexed by RGB555, like gpSP's gba_cc_lut. */
static const u16 *vt_cc_lut;
static volatile int vt_force_pal;
static int vt_depth[4];
static int vt_obj_depth[4];    /* sprites can sit nearer than BGs of the same priority */
static volatile int vt_tilt16;  /* floor tilt: extra depth at the bottom line, 1/16 px */
static volatile int vt_fill_gaps = 1;
static volatile int vt_smart = 1;      /* layers that scroll together share a depth */
static volatile int vt_fast3d = 0;     /* build both eyes from one drawn picture */

/* Horizontal shift of one eye's image for a layer at depth d: the two eyes
 * differ by d pixels in total; the left eye gets the odd pixel. */
static inline int vt_eye_shift(int d, int eye)
{
  int right = -(d / 2);
  return eye ? right : d + right;
}

/* One VRAM copy, refreshed only when the game wrote VRAM. A write in the
 * middle of a frame first lets the render thread catch up (it is still
 * reading the old contents for earlier lines), then copies. */
/* A small pool of VRAM copies. A write in the middle of a frame takes the
 * next copy, so the emulation only waits when every copy is still in use by
 * lines the render thread has not drawn yet. */
#define VT_NVRAM   6
#define VT_VRAM_SZ (1024 * 96)
static u8 *vt_vram_pool;
#define VT_VRAM(i) (vt_vram_pool + (i) * VT_VRAM_SZ)
static unsigned vt_vram_cur;                 /* copy used by new lines */
static unsigned vt_vram_last_use[VT_NVRAM];  /* lines_pushed after its last line */
/* Per copy: 4KB blocks where it differs from the live VRAM (bit n = block n,
 * filled from the emulator's REG_VRAM_UPDATED dirty mask). */
static u32 vt_vram_stale[VT_NVRAM];
static unsigned vram_copies, vram_syncs;
#define VT_ALL_BLOCKS 0xFFFFFFu

/* 4KB VRAM blocks the renderer can read for a line with these registers */
static u32 vt_blocks_read(void)
{
  u16 dispcnt = read_ioreg(REG_DISPCNT);
  u32 mode = dispcnt & 7, m = 0;
  if (dispcnt & 0x80)                 /* forced blank: draws nothing */
    return 0;
  if (dispcnt & 0x1000)               /* sprites */
    m |= mode >= 3 ? 0xF00000u : 0xFF0000u;   /* 0x14000.. / 0x10000.. */
  if (mode < 3)
  {
    if (dispcnt & 0x0F00)
      m |= 0x00FFFFu;                 /* tiled BGs: anything in 0..64KB */
  }
  else if (dispcnt & 0x0400)          /* bitmap modes draw BG2 only */
  {
    if (mode == 3)
      m |= 0x07FFFFu;                 /* 0x00000-0x12BFF */
    else if (dispcnt & 0x10)
      m |= 0x0FFC00u;                 /* page 1: 0x0A000-0x13FFF */
    else
      m |= 0x0003FFu;                 /* page 0: 0x00000-0x09FFF */
  }
  return m;
}

/* bring copy i up to date with the live VRAM (only its stale blocks) */
static void vt_vram_refresh(unsigned i)
{
  u32 st = vt_vram_stale[i];
  u8 *dst = VT_VRAM(i);
  while (st)
  {
    unsigned b = __builtin_ctz(st), n = 0;
    /* copy runs of consecutive blocks in one go */
    while (b + n < 24 && (st & (1u << (b + n))))
      n++;
    memcpy(dst + b * 4096, vram + b * 4096, n * 4096);
    st &= ~(((1u << n) - 1) << b);
    vram_copies++;
  }
  vt_vram_stale[i] = 0;
}
static unsigned lines_pushed;                /* emulation side */
static volatile unsigned lines_done;         /* render side */

volatile int video_thread_enabled = 0;

static vt_frame *slots[2];
static vt_sem   *sem_line;          /* one post per queued message */
static vt_sem   *sem_done;          /* one post per finished frame */
static vt_sem   *sem_sync;          /* render thread caught up */
static u16       ring[VT_RING];
static volatile unsigned ring_wr;
static unsigned  ring_rd;

/* Optional: called on the render thread when a frame is finished, before the
 * emulation thread is released (e.g. to copy it to the screen there). */
static void (*frame_hook)(const u16 *left, const u16 *right, unsigned pitch, void *arg);
static void *frame_hook_arg;
static volatile unsigned hook_us_last;

static int       rec_slot;          /* slot the emulation thread records into */
static volatile int end_slot;       /* slot of the frame being finished */
static int defer_req, deferred_pending;   /* see video_thread_defer_next_finish */
static int have_frame;                    /* a frame has been finished since start */
static unsigned  rec_lines;         /* lines recorded in the current frame */

static unsigned  render_us_acc;     /* render thread: time spent this frame */
static volatile unsigned render_us_last;
static unsigned  wait_us_last;

static inline void vt_barrier(void) { __sync_synchronize(); }

/* Waking the render thread is a system call, so lines are handed over in
 * batches of 8; control messages (sync/end/quit) always wake it at once. */
#define VT_WAKE_EVERY 8

static void vt_push(u16 msg)
{
  ring[ring_wr & (VT_RING - 1)] = msg;
  vt_barrier();
  ring_wr = ring_wr + 1;
  vt_barrier();
  if (msg >= VT_MSG_SYNC || (ring_wr % VT_WAKE_EVERY) == 0)
    vt_sem_post(sem_line);
}

/* ------------------------------------------------------------------ */
/* Render thread                                                       */
/* ------------------------------------------------------------------ */

namespace rt {
/* 3D gap fill. Games often build one surface from two BG layers that fit
 * together (one layer's tiles exactly where the other has holes). Layers at
 * different depths slide apart by a pixel or two and the backdrop colour
 * shows through the seam. The backdrop is drawn with a colour no palette
 * entry uses, so those pixels can be found and short runs filled from their
 * neighbours. Only on lines without colour effects (the sentinel must not be
 * blended). */
static u16 vt_sentinel;
static int vt_sentinel_ok;

static void pick_sentinel(void)
{
  static const u16 cand[] = { 0x0841, 0x1082, 0x18C3, 0x2104, 0x0821, 0x0861, 0xF7DE, 0x7BCF };
  unsigned c, i;
  for (c = 0; c < sizeof(cand) / sizeof(cand[0]); c++)
  {
    for (i = 0; i < 512; i++)
      if (palette_ram_converted[i] == cand[c])
        break;
    if (i == 512)
    {
      vt_sentinel = cand[c];
      vt_sentinel_ok = 1;
      return;
    }
  }
  vt_sentinel_ok = 0;
}

/* probe: line drawn with the sentinel backdrop (gaps = sentinel runs);
 * p: line to repair (may be the probe itself) */
static void fill_gaps(u16 *p, const u16 *probe, u16 bd, int maxgap)
{
  int x = 0;
  const u16 sn = vt_sentinel;
  while (x < 240)
  {
    int start, len, i;
    if (probe[x] != sn) { x++; continue; }
    start = x;
    while (x < 240 && probe[x] == sn)
      x++;
    len = x - start;
    if (len <= maxgap && (start > 0 || x < 240))
    {
      u16 l = start > 0 ? p[start - 1] : p[x];
      u16 r = x < 240 ? p[x] : p[start - 1];
      for (i = 0; i < len; i++)
        p[start + i] = i < (len + 1) / 2 ? l : r;
    }
    else if (p == probe)
      for (i = start; i < x; i++)
        p[i] = bd;               /* real backdrop area: its true colour */
  }
}

static void draw_line(vt_frame *f, unsigned vcount)
{
  const vt_line *l = &f->lines[vcount];
  u16 dispcnt;
  u32 video_mode;
  u16 *screen_offset;

  memcpy(io_registers, l->io, sizeof(l->io));
  affine_reference_x[0] = l->affx[0];
  affine_reference_x[1] = l->affx[1];
  affine_reference_y[0] = l->affy[0];
  affine_reference_y[1] = l->affy[1];
  if (l->has_pal)
  {
    if (vt_cc_lut)
    {
      unsigned i;
      for (i = 0; i < 512; i++)
      {
        u16 c = l->pal[i];
        palette_ram_converted[i] = vt_cc_lut[((c & 0xFFC0) >> 1) | (c & 0x1F)];
      }
    }
    else
      memcpy(palette_ram_converted, l->pal, sizeof(l->pal));
  }
  if (l->has_oam)
  {
    memcpy(oam_ram, l->oam, sizeof(l->oam));
    reg[OAM_UPDATED] = 1;
  }
  vram = VT_VRAM(l->vram_idx);
  gba_screen_pixels = f->pixels;

  /* Same as the drawing half of update_scanline() */
  dispcnt = read_ioreg(REG_DISPCNT);
  video_mode = dispcnt & 0x07;
  screen_offset = gba_screen_pixels + vcount * GBA_SCREEN_PITCH;

  if (reg[OAM_UPDATED])
  {
    order_obj(video_mode);
    reg[OAM_UPDATED] = 0;
  }

  order_layers((dispcnt >> 8) & active_layers[video_mode], vcount);

  if (dispcnt & 0x80)
  {
    memset(screen_offset, 0xff, 240 * sizeof(u16));
    if (vt_stereo)
      memset(f->pixels_r + vcount * GBA_SCREEN_PITCH, 0xff, 240 * sizeof(u16));
    return;
  }

  if (!vt_stereo)
  {
    render_scanline_window(screen_offset);
    return;
  }

  /* 3D. Depth per BG / sprite priority (with Smart grouping), then either
   * draw the line once per eye with every layer shifted (Accurate), or draw
   * it once and move each pixel by its layer's depth (Fast). */
  {
    int bgd[4], objd[4], prio[4], n, eye;
    u16 hofs[4];
    s32 ax[2] = { affine_reference_x[0], affine_reference_x[1] };
    s32 ay[2] = { affine_reference_y[0], affine_reference_y[1] };

    for (n = 0; n < 4; n++)
    {
      hofs[n] = read_ioreg(REG_BGxHOFS(n));
      prio[n] = read_ioreg(REG_BGxCNT(n)) & 3;
      bgd[n] = vt_depth[prio[n]];
      objd[n] = vt_obj_depth[n];
    }

    if (vt_smart)
    {
      /* Text BGs with the same scroll and tile data are one picture split
       * over several layers (e.g. a Pokemon map): give them one depth, the
       * front-most one's. Sprites follow the depth of the BG group at their
       * priority, plus their own pop. */
      u32 mode = dispcnt & 7, en = (dispcnt >> 8) & 0xF;
      int textbgs = mode == 0 ? 4 : mode == 1 ? 2 : 0, eff[4], m, k;
      for (n = 0; n < 4; n++)
        eff[n] = vt_depth[n];
      for (n = 0; n < textbgs; n++)
      {
        int best = prio[n];
        if (!(en & (1 << n)))
          continue;
        for (m = 0; m < textbgs; m++)
          if (m != n && (en & (1 << m)) &&
              read_ioreg(REG_BGxHOFS(m)) == hofs[n] &&
              read_ioreg(REG_BGxVOFS(m)) == read_ioreg(REG_BGxVOFS(n)) &&
              ((read_ioreg(REG_BGxCNT(m)) >> 2) & 3) == ((read_ioreg(REG_BGxCNT(n)) >> 2) & 3) &&
              prio[m] < best)
            best = prio[m];
        bgd[n] = vt_depth[best];
      }
      for (k = 0; k < 4; k++)
        for (n = 0; n < textbgs; n++)
          if ((en & (1 << n)) && prio[n] == k)
            eff[k] = bgd[n];
      for (k = 0; k < 4; k++)
        objd[k] = eff[k] + (vt_obj_depth[k] - vt_depth[k]);
    }

    if (vt_tilt16)
    {
      /* Floor tilt: the bottom of the screen comes nearer and the top goes
       * further, so a road or floor drawn on one layer recedes. */
      int t = vt_tilt16 * ((int)vcount - 80) / 80;
      t = t < 0 ? -((8 - t) >> 4) : (t + 8) >> 4;
      for (n = 0; n < 4; n++)
      {
        bgd[n] += t;
        objd[n] += t;
      }
    }

    if (vt_fast3d)
    {
      /* draw once, remembering each pixel's layer */
      static u16 mono[GBA_SCREEN_PITCH];
      int dz[9], far = 99;
      for (n = 0; n < 4; n++)
      {
        dz[n] = bgd[n];
        dz[4 + n] = objd[n];
        if (bgd[n] < far) far = bgd[n];
        if (objd[n] < far) far = objd[n];
      }
      dz[8] = far;
      memset(rt_layer_id, 8, sizeof(rt_layer_id));
      rt_seg_ptr = NULL;
      rt_track = 1;
      render_scanline_window(mono);
      rt_track = 0;

      {
        /* runs of pixels from the same layer */
        static u8 rs[240], rl[240], ri[240];
        int nr = 0, x = 0, order[9], no = 0, i, j;
        while (x < 240)
        {
          int id = rt_layer_id[x], st = x;
          while (x < 240 && rt_layer_id[x] == id)
            x++;
          rs[nr] = (u8)st; rl[nr] = (u8)(x - st); ri[nr] = (u8)id; nr++;
        }
        /* layers from far to near: nearer ones are copied last and win */
        for (i = 0; i < 9; i++)
          order[no++] = i;
        for (i = 1; i < no; i++)
          for (j = i; j > 0 && dz[order[j - 1]] > dz[order[j]]; j--)
          { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }

        for (eye = 0; eye < 2; eye++)
        {
          u16 *out = (eye ? f->pixels_r : f->pixels) + vcount * GBA_SCREEN_PITCH;
          s8 z[240];
          int sh[9], k, last;
          for (n = 0; n < 9; n++)
            sh[n] = vt_eye_shift(dz[n], eye);
          memset(z, -128, sizeof(z));
          for (k = 0; k < no; k++)
          {
            int id = order[k];
            for (i = 0; i < nr; i++)
            {
              int a0, a1;
              if (ri[i] != id)
                continue;
              a0 = rs[i] + sh[id];
              a1 = a0 + rl[i];
              {
                int src = rs[i];
                if (a0 < 0) { src -= a0; a0 = 0; }
                if (a1 > 240) a1 = 240;
                if (a1 > a0)
                {
                  memcpy(out + a0, mono + src, (a1 - a0) * 2);
                  memset(z + a0, (s8)dz[id], a1 - a0);
                }
              }
            }
          }
          /* uncovered runs: continue the further-away neighbour */
          last = -1;
          for (x = 0; x < 240; x++)
          {
            int r;
            u16 c;
            if (z[x] != -128) { last = x; continue; }
            r = x;
            while (r < 240 && z[r] == -128)
              r++;
            if (last >= 0 && (r >= 240 || z[last] <= z[r]))
              c = out[last];
            else if (r < 240)
              c = out[r];
            else
              c = mono[x];
            for (; x < r; x++)
              out[x] = c;
            x = r - 1;
          }
        }
      }
      return;
    }

    if (l->has_pal || !vt_sentinel_ok)
      pick_sentinel();

    for (eye = 0; eye < 2; eye++)
    {
      u16 *base = eye ? f->pixels_r : f->pixels;
      int smin = 99, smax = -99, fill, unsafe;
      u16 bd = palette_ram_converted[0];
      for (n = 0; n < 4; n++)
      {
        int sh = vt_eye_shift(bgd[n], eye);
        io_registers[REG_BGxHOFS(n)] = eswap16((u16)((hofs[n] - sh) & 0x1FF));
        if (n >= 2)
        {
          affine_reference_x[n - 2] = ax[n - 2] - sh * (s32)(s16)read_ioreg(REG_BGxPA(n));
          affine_reference_y[n - 2] = ay[n - 2] - sh * (s32)(s16)read_ioreg(REG_BGxPC(n));
        }
        vt_eye_obj_shift[n] = vt_eye_shift(objd[n], eye);
        if (sh < smin) smin = sh;
        if (sh > smax) smax = sh;
        if (vt_eye_obj_shift[n] < smin) smin = vt_eye_obj_shift[n];
        if (vt_eye_obj_shift[n] > smax) smax = vt_eye_obj_shift[n];
      }
      {
        /* Simple case: the sentinel backdrop can't be blended into another
         * colour, so one pass both draws the line and marks the gaps.
         * Otherwise (backdrop is a blend target) the line is drawn normally
         * and a second, effect-free pass only finds the gaps. */
        u16 bldcnt = read_ioreg(REG_BLDCNT);
        u32 effect = (bldcnt >> 6) & 3;
        bool bd_2nd = (bldcnt >> 13) & 1, bd_1st = (bldcnt >> 5) & 1;
        unsafe = (bd_2nd && (effect == 1 || obj_alpha_count[vcount])) ||
                 (bd_1st && effect >= 2);
        fill = vt_fill_gaps && vt_sentinel_ok && smax > smin;
      }
      gba_screen_pixels = base;
      if (fill && !unsafe)
      {
        u16 *line = base + vcount * GBA_SCREEN_PITCH;
        palette_ram_converted[0] = vt_sentinel;
        render_scanline_window(line);
        palette_ram_converted[0] = bd;
        fill_gaps(line, line, bd, smax - smin);
      }
      else
      {
        u16 *line = base + vcount * GBA_SCREEN_PITCH;
        render_scanline_window(line);
        if (fill)
        {
          static u16 probe[GBA_SCREEN_PITCH];
          u16 bldcnt = io_registers[REG_BLDCNT];
          io_registers[REG_BLDCNT] = 0;            /* no effects: pure layering */
          palette_ram_converted[0] = vt_sentinel;
          render_scanline_window(probe);
          palette_ram_converted[0] = bd;
          io_registers[REG_BLDCNT] = bldcnt;
          fill_gaps(line, probe, bd, smax - smin);
        }
      }
    }

    for (n = 0; n < 4; n++)
    {
      io_registers[REG_BGxHOFS(n)] = eswap16(hofs[n]);
      vt_eye_obj_shift[n] = 0;
    }
    affine_reference_x[0] = ax[0]; affine_reference_x[1] = ax[1];
    affine_reference_y[0] = ay[0]; affine_reference_y[1] = ay[1];
    gba_screen_pixels = f->pixels;
  }
}
}

static void render_thread(void *arg)
{
  (void)arg;
  for (;;)
  {
    u16 msg;
    vt_barrier();
    if (ring_rd == ring_wr)
    {
      /* nothing queued: sleep until the next batch or control message */
      vt_sem_wait(sem_line);
      continue;
    }
    vt_barrier();
    msg = ring[ring_rd & (VT_RING - 1)];
    ring_rd++;

    if (msg == VT_MSG_QUIT)
      break;
    if (msg == VT_MSG_SYNC)
    {
      vt_barrier();
      vt_sem_post(sem_sync);
      continue;
    }
    if (msg == VT_MSG_END)
    {
      if (frame_hook)
      {
        unsigned long long t0 = vt_now_us();
        frame_hook(slots[end_slot]->pixels, vt_stereo ? slots[end_slot]->pixels_r : NULL,
                   GBA_SCREEN_PITCH, frame_hook_arg);
        hook_us_last = (unsigned)(vt_now_us() - t0);
      }
      render_us_last = render_us_acc;
      render_us_acc = 0;
      vt_barrier();
      vt_sem_post(sem_done);
      continue;
    }

    {
      unsigned long long t0 = vt_now_us();
      rt::draw_line(slots[msg >> 8], msg & 0xFF);
      render_us_acc += (unsigned)(vt_now_us() - t0);
      vt_barrier();
      lines_done++;
    }
  }
}

/* ------------------------------------------------------------------ */
/* Emulation side                                                      */
/* ------------------------------------------------------------------ */

void video_thread_record_line(unsigned vcount)
{
  vt_frame *f = slots[rec_slot];
  vt_line *l;

  if (vcount >= VT_LINES)
    return;

  /* a deferred frame must be done before this one starts (VRAM is
   * refreshed in place at the start of a frame) */
  if (deferred_pending && !rec_lines)
    video_thread_complete_deferred();

  if (reg[REG_VRAM_UPDATED])
  {
    u32 dirty = reg[REG_VRAM_UPDATED] & VT_ALL_BLOCKS, i;
    reg[REG_VRAM_UPDATED] = 0;
    for (i = 0; i < VT_NVRAM; i++)
      vt_vram_stale[i] |= dirty;
  }
  /* Only blocks this line can read matter: writes elsewhere (e.g. the back
   * buffer of a bitmap-mode game) wait until they are needed. */
  if (vt_vram_stale[vt_vram_cur] & vt_blocks_read())
  {
    if (rec_lines)
    {
      /* mid-frame write: move to the next copy; earlier lines keep theirs.
       * Only wait if that copy is still needed by undrawn lines. */
      unsigned next = (vt_vram_cur + 1) % VT_NVRAM;
      vt_barrier();
      if ((int)(lines_done - vt_vram_last_use[next]) < 0)
      {
        vt_push(VT_MSG_SYNC);
        vt_sem_wait(sem_sync);
        vt_barrier();
        vram_syncs++;
      }
      vt_vram_cur = next;
    }
    /* at the start of a frame the render thread is idle (finish_frame
     * waited for it), so the current copy can be refreshed in place */
    vt_vram_refresh(vt_vram_cur);
  }

  if (!rec_lines)
    memset(f->recorded, 0, sizeof(f->recorded));
  f->recorded[vcount] = 1;
  l = &f->lines[vcount];
  memcpy(l->io, io_registers, sizeof(l->io));
  l->affx[0] = affine_reference_x[0];
  l->affx[1] = affine_reference_x[1];
  l->affy[0] = affine_reference_y[0];
  l->affy[1] = affine_reference_y[1];

  /* the first line of a frame always carries the palette and OAM, so the
   * frame can be drawn again on its own (video_thread_redraw) */
  l->has_pal = reg[REG_PAL_UPDATED] != 0 || vt_force_pal || rec_lines == 0;
  vt_force_pal = 0;
  if (l->has_pal)
  {
    memcpy(l->pal, palette_ram_converted, sizeof(l->pal));
    reg[REG_PAL_UPDATED] = 0;
  }
  l->has_oam = reg[OAM_UPDATED] != 0 || rec_lines == 0;
  if (l->has_oam)
  {
    memcpy(l->oam, oam_ram, sizeof(l->oam));
    reg[OAM_UPDATED] = 0;
  }

  l->vram_idx = (u8)vt_vram_cur;
  rec_lines++;
  lines_pushed++;
  vt_vram_last_use[vt_vram_cur] = lines_pushed;
  vt_push((u16)((rec_slot << 8) | vcount));
}

/* Deferred finish: the frontend says the next frame will be skipped, so the
 * emulation thread need not wait for this one; it is completed (and its
 * frame hook has run) by video_thread_complete_deferred(). */

void video_thread_defer_next_finish(void) { defer_req = 1; }

int video_thread_complete_deferred(void)
{
  unsigned long long t0;
  if (!deferred_pending)
    return 0;
  t0 = vt_now_us();
  vt_sem_wait(sem_done);
  vt_barrier();
  wait_us_last = (unsigned)(vt_now_us() - t0);
  deferred_pending = 0;
  return 1;
}

const unsigned short *video_thread_finish_frame(void)
{
  unsigned long long t0;
  const u16 *pixels;

  wait_us_last = 0;   /* no wait unless this frame is waited for below */
  if (!video_thread_enabled || !rec_lines)
  {
    defer_req = 0;
    return NULL;
  }

  if (defer_req)
  {
    defer_req = 0;
    end_slot = rec_slot;
    have_frame = 1;
    vt_barrier();
    vt_push(VT_MSG_END);
    deferred_pending = 1;
    rec_slot ^= 1;
    rec_lines = 0;
    return NULL;
  }

  t0 = vt_now_us();
  end_slot = rec_slot;
  have_frame = 1;
  vt_barrier();
  vt_push(VT_MSG_END);
  vt_sem_wait(sem_done);
  vt_barrier();
  wait_us_last = (unsigned)(vt_now_us() - t0);

  pixels = slots[rec_slot]->pixels;
  rec_slot ^= 1;
  rec_lines = 0;
  return pixels;
}

unsigned video_thread_last_render_us(void) { return render_us_last; }
unsigned video_thread_last_hook_us(void)   { return hook_us_last; }

void video_thread_set_color_correction(const unsigned short *lut)
{
  vt_cc_lut = (const u16*)lut;
  vt_force_pal = 1;   /* next line re-sends the palette through the table */
}

void video_thread_set_gap_fill(int on) { vt_fill_gaps = on; }
void video_thread_set_smart_layers(int on) { vt_smart = on; }
void video_thread_set_fast3d(int on) { vt_fast3d = on; }
void video_thread_set_tilt(int tilt16) { vt_tilt16 = tilt16; }

void video_thread_set_stereo(const int depth[4])
{
  video_thread_set_stereo2(depth, depth);
}

void video_thread_set_stereo2(const int bg_depth[4], const int obj_depth[4])
{
  int n;
  for (n = 0; n < 4; n++)
  {
    vt_depth[n]     = bg_depth ? bg_depth[n] : 0;
    vt_obj_depth[n] = bg_depth ? (obj_depth ? obj_depth[n] : bg_depth[n]) : 0;
  }
  vt_stereo = bg_depth != NULL;
}

/* Draw the last finished frame again (e.g. after changing 3D or colour
 * settings while the game is paused) and run the frame hook on it. Only
 * between frames, with no frame being recorded. Returns 0 if there is no
 * frame to redraw. */
int video_thread_redraw(void)
{
  vt_frame *f;
  unsigned n, pushed = 0;
  if (!video_thread_enabled || rec_lines || deferred_pending || !have_frame)
    return 0;
  f = slots[end_slot];
  for (n = 0; n < VT_LINES; n++)
    if (f->recorded[n])
    {
      vt_push((u16)((end_slot << 8) | n));
      pushed++;
    }
  if (!pushed)
    return 0;
  vt_barrier();
  vt_push(VT_MSG_END);
  vt_sem_wait(sem_done);
  vt_barrier();
  return 1;
}

void video_thread_set_frame_hook(void (*hook)(const unsigned short *left, const unsigned short *right, unsigned pitch, void *arg), void *arg)
{
  frame_hook = hook;
  frame_hook_arg = arg;
}
unsigned video_thread_last_wait_us(void)   { return wait_us_last; }
void video_thread_set_low_priority(int low) { vt_thread_set_low_priority(low); }
void video_thread_counters(unsigned *copies, unsigned *syncs) { *copies = vram_copies; *syncs = vram_syncs; }

int video_thread_start(int core)
{
  if (video_thread_enabled)
    return 0;

  have_frame = 0;
  deferred_pending = defer_req = 0;
  slots[0] = (vt_frame*)calloc(1, sizeof(vt_frame));
  slots[1] = (vt_frame*)calloc(1, sizeof(vt_frame));
  sem_line = vt_sem_create();
  sem_done = vt_sem_create();
  sem_sync = vt_sem_create();
  vt_vram_pool = (u8*)malloc(VT_NVRAM * VT_VRAM_SZ);
  if (!slots[0] || !slots[1] || !sem_line || !sem_done || !sem_sync || !vt_vram_pool)
    goto fail;

  ring_wr = ring_rd = 0;
  rec_slot = 0;
  rec_lines = 0;
  render_us_acc = render_us_last = wait_us_last = 0;

  /* The render copy starts empty: make the first line carry everything. */
  reg[REG_PAL_UPDATED] = 1;
  reg[REG_VRAM_UPDATED] = VT_ALL_BLOCKS;
  reg[OAM_UPDATED] = 1;
  {
    int i;
    for (i = 0; i < VT_NVRAM; i++)
      vt_vram_stale[i] = VT_ALL_BLOCKS;
  }
  vram_copies = vram_syncs = 0;
  vt_vram_cur = 0;
  lines_pushed = lines_done = 0;
  memset(vt_vram_last_use, 0, sizeof(vt_vram_last_use));

  if (vt_thread_start(render_thread, NULL, core))
    goto fail;

  video_thread_enabled = 1;
  return 0;

fail:
  free(slots[0]); free(slots[1]); free(vt_vram_pool);
  slots[0] = slots[1] = NULL; vt_vram_pool = NULL;
  vt_sem_destroy(sem_line); vt_sem_destroy(sem_done); vt_sem_destroy(sem_sync);
  sem_line = sem_done = sem_sync = NULL;
  return -1;
}

void video_thread_stop(void)
{
  if (!video_thread_enabled)
    return;
  video_thread_complete_deferred();
  if (rec_lines)
    video_thread_finish_frame();
  vt_push(VT_MSG_QUIT);
  vt_thread_join();
  video_thread_enabled = 0;
  free(slots[0]); free(slots[1]); free(vt_vram_pool);
  slots[0] = slots[1] = NULL; vt_vram_pool = NULL;
  vt_sem_destroy(sem_line); vt_sem_destroy(sem_done); vt_sem_destroy(sem_sync);
  sem_line = sem_done = sem_sync = NULL;
  /* the normal renderer must re-sort sprites before its next line */
  reg[OAM_UPDATED] = 1;
}
