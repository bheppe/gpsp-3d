# GBA test ROMs for the dynarec

Small hand-written GBA programs that check gpSP's ARM dynarec against its interpreter. Run them after any change to the dynarec or the CPU code:

```
bash tests/gba-roms/run.sh
```

The script builds the core for ARM Linux with Raspberry Pi 1 settings (an ARM11, like the 3DS, using the same 32-bit ARM dynarec) and runs it under QEMU. It needs `gcc-arm-none-eabi`, `gcc-arm-linux-gnueabihf`, `g++-arm-linux-gnueabihf`, `qemu-user` and `rsync`.

| ROM | What it checks |
|---|---|
| `smc.s` | Code that changes code in RAM, as in Mario Golf: Advance Tour: a loop patched before every call, a variable inside a code block, code copied in by DMA, calls into a patched routine in another page, a block across a page boundary, Thumb and EWRAM code, and store-multiple patches across a page boundary. Every checksum must match the interpreter's. |
| `fiq.s` | FIQ mode keeps its own r8-r12 while IRQs come in (OpenLara draws in FIQ mode). Green screen = pass. |

`harness.c` is a minimal libretro frontend: `harness ROM dynarec(0/1) frames [out.ppm]`. It prints the RAM flush count and the first words of EWRAM, where `smc.s` leaves its checksums.
