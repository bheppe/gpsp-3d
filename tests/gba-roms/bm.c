/* Bitmap modes drawn by the render thread while the CPU writes VRAM during
 * the frame (like the Super Mario Galaxy GBA port: mode 4 at 120x80 scaled
 * 2x, two pages side by side in the same rows). The threaded renderer must
 * give exactly the single-threaded picture. Phases of 40 frames: scaled
 * mode 4, rotated mode 4, scaled mode 3, mode 5 with page flips. */
typedef unsigned short u16; typedef unsigned int u32; typedef unsigned char u8;
typedef short s16;
#define REG(a) (*(volatile u16*)(a))
#define REG32(a) (*(volatile u32*)(a))
#define DISPCNT REG(0x04000000)
#define VCOUNT  REG(0x04000006)
#define BG2PA REG(0x04000020)
#define BG2PB REG(0x04000022)
#define BG2PC REG(0x04000024)
#define BG2PD REG(0x04000026)
#define BG2X REG32(0x04000028)
#define BG2Y REG32(0x0400002C)
#define PAL ((volatile u16*)0x05000000)
#define OAM ((volatile u16*)0x07000000)
#define VRAM16 ((volatile u16*)0x06000000)
static u32 frames, seed = 12345;
static unsigned last_v;
static u32 rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static void tick(void) { unsigned v = VCOUNT; if (v >= 160 && last_v < 160) frames++; last_v = v; }
int main(void) {
  u32 i, f = 0, phase = 99;
  for (i = 0; i < 256; i++) { PAL[i] = (u16)(i * 0x0421u + i); PAL[256 + i] = (u16)(0x7FFF - i * 33); }
  for (i = 0; i < 32 * 16; i++) VRAM16[0x14000 / 2 + i] = (u16)(0x1111u * (1 + (i & 7)));   /* OBJ tiles (bitmap modes: 0x14000+) */
  OAM[0] = 20; OAM[1] = 30 | 0x4000; OAM[2] = 512;    /* a 16x16 sprite */
  for (;;) {
    u32 p;
    tick();
    p = (frames / 40) & 3;
    if (frames != f) {                       /* new frame: flip pages, move */
      f = frames;
      if (p != phase) phase = p;
      if (p == 0) { DISPCNT = 0x1000 | 0x0400 | 0x0040 | 4; BG2PA = 128; BG2PB = 0; BG2PC = 0; BG2PD = 128; BG2X = (f & 1) ? 120 << 8 : 0; BG2Y = 0; }
      if (p == 1) { DISPCNT = 0x1000 | 0x0400 | 0x0040 | 4; BG2PA = 200; BG2PB = 60; BG2PC = (u16)(s16)-60; BG2PD = 200; BG2X = (f & 15) << 8; BG2Y = 0; }
      if (p == 2) { DISPCNT = 0x1000 | 0x0400 | 0x0040 | 3; BG2PA = 192; BG2PB = 0; BG2PC = 0; BG2PD = 192; BG2X = (f & 31) << 8; BG2Y = (f & 7) << 8; }
      if (p == 3) { DISPCNT = 0x1000 | 0x0400 | 0x0040 | 5 | ((f & 1) << 4); BG2PA = 256; BG2PB = 0; BG2PC = 0; BG2PD = 256; BG2X = 0; BG2Y = 0; }
      OAM[1] = (u16)(((20 + f) & 255) | 0x4000);
    }
    /* write VRAM all through the frame: mostly the hidden page / area, but
     * now and then a pixel that is on screen (must show only from the line
     * being drawn when it was written) */
    for (i = 0; i < 16; i++) {
      u32 r = rnd(), y = r % 160, x = (r >> 8) % 120, v = (r >> 16) & 0xFFFF;
      u32 off;
      if (p == 0 || p == 1) {                /* mode 4: 240 bytes per row */
        u32 col = (r & 0x800000) ? x : x + 120;
        off = y * 240 + (col & ~1u);
        VRAM16[off / 2] = (u16)v;
      } else if (p == 2) {
        VRAM16[y * 240 + x * 2] = (u16)v;
      } else {
        VRAM16[((r >> 24) & 1 ? 0x5000 : 0) + (y & 127) * 160 + x] = (u16)v;
      }
    }
    if ((rnd() & 1023) == 0)                 /* sprite tiles change too */
      VRAM16[0x14000 / 2 + (rnd() & 255)] = (u16)rnd();
  }
}
