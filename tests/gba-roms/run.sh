#!/bin/bash
# Runs the test ROMs on gpSP's 32-bit ARM dynarec (the one gpSP 3D uses on
# the 3DS) and compares the results with the interpreter.
#
# Needs (Ubuntu/Debian): gcc-arm-none-eabi, gcc-arm-linux-gnueabihf,
# g++-arm-linux-gnueabihf, qemu-user, rsync. Run from anywhere:
#   bash tests/gba-roms/run.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
CORE="$(cd "$HERE/../.." && pwd)"
WORK="${WORK:-/tmp/gpsp-rom-tests}"
export QEMU_LD_PREFIX=/usr/arm-linux-gnueabihf

rm -rf "$WORK" && mkdir -p "$WORK/core"
echo "== test ROMs"
arm-none-eabi-gcc -mcpu=arm7tdmi -nostdlib -T "$HERE/smc.ld" "$HERE/smc.s" -o "$WORK/smc.elf"
arm-none-eabi-objcopy -O binary "$WORK/smc.elf" "$WORK/smc.gba"
arm-none-eabi-gcc -mcpu=arm7tdmi -nostdlib -T "$HERE/gba.ld" "$HERE/fiq.s" -o "$WORK/fiq.elf"
arm-none-eabi-objcopy -O binary -j .text "$WORK/fiq.elf" "$WORK/fiq.gba"
arm-none-eabi-gcc -mcpu=arm7tdmi -marm -O2 -nostdlib -T "$HERE/gba.ld" "$HERE/crt0.s" "$HERE/bm.c" -o "$WORK/bm.elf"
arm-none-eabi-objcopy -O binary "$WORK/bm.elf" "$WORK/bm.gba"

echo "== core (ARM Linux, Raspberry Pi 1 settings = ARM11 like the 3DS)"
rsync -a --exclude '*.o' --exclude '*.so' --exclude .git "$CORE"/ "$WORK/core/"
cd "$WORK/core"
# a RAM flush total the runner can read (only in this test build)
sed -i 's/^  flush_ram_count++;$/  flush_ram_count++; { extern u32 test_flush_total; test_flush_total++; }/' cpu_threaded.c
grep -q test_flush_total cpu_threaded.c
echo 'u32 test_flush_total;' >> main.c
make platform=rpi1 CC=arm-linux-gnueabihf-gcc CXX=arm-linux-gnueabihf-g++ PROFILE=1 -j"$(nproc)" > "$WORK/build.log" 2>&1 \
  || { grep -E 'error' "$WORK/build.log" | head; exit 1; }
OBJS=$(ls *.o arm/*.o libretro/*.o libretro/libretro-common/*/*.o)
arm-linux-gnueabihf-gcc -O1 -Ilibretro -Ilibretro/libretro-common/include "$HERE/harness.c" $OBJS \
  -lstdc++ -lpthread -lm -o "$WORK/harness"

fail=0
echo "== self-modifying code (smc.gba)"
ref=$(qemu-arm "$WORK/harness" "$WORK/smc.gba" 0 500)
drc=$(qemu-arm "$WORK/harness" "$WORK/smc.gba" 1 500)
echo "  interpreter: ${ref#*results:}"
echo "  dynarec:     ${drc#*results:}"
echo "  dynarec $(echo "$drc" | grep -o 'flushes=[0-9]*') (full RAM flushes)"
if [ "${ref#*results:}" = "${drc#*results:}" ]; then echo "  PASS"; else echo "  FAIL"; fail=1; fi

echo "== FIQ banking (fiq.gba)"
qemu-arm "$WORK/harness" "$WORK/fiq.gba" 1 600 "$WORK/fiq.ppm" > /dev/null
if python3 -c "import sys; d=open('$WORK/fiq.ppm','rb').read()[15:]; sys.exit(0 if tuple(d[0:3])==(0,248,0) else 1)"
then echo "  PASS (green)"; else echo "  FAIL (not green)"; fail=1; fi
echo "== bitmap modes on the render thread (bm.gba)"
one=$(HASH=1 THREAD=0 qemu-arm "$WORK/harness" "$WORK/bm.gba" 1 170 | grep -o 'hash=[0-9A-F]*')
two=$(HASH=1 THREAD=1 qemu-arm "$WORK/harness" "$WORK/bm.gba" 1 170 | grep -o 'hash=[0-9A-F]*')
echo "  single-threaded $one, render thread $two"
if [ "$one" = "$two" ]; then echo "  PASS"; else echo "  FAIL"; fail=1; fi
exit $fail
