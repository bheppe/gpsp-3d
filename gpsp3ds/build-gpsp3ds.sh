#!/bin/bash
# Builds "gpSP 3D", the standalone gpSP app for 3DS, as gpsp3d.cia and gpsp3d.3dsx.
# Run in the devkitPro shell (MSYS2 on Windows), from anywhere:
#   bash path/to/gpsp3ds/build-gpsp3ds.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
# Everything is also written to build.log so progress can be checked from outside the shell
exec > >(tee "$HERE/build.log") 2>&1
echo "started $(date)"

export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export CTRULIB=${CTRULIB:-$DEVKITPRO/libctru}
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
JOBS=$(nproc 2>/dev/null || echo 4)

for t in arm-none-eabi-gcc bannertool makerom smdhtool 3dsxtool picasso raw2c; do
  command -v $t >/dev/null || { echo "MISSING: $t (check devkitPro / tools/bin)"; exit 1; }
done

echo "=== 1/3 gpSP core ==="
# The core is this repo's root (or a sibling "gpsp" folder in older layouts)
# (CORE is relative to this folder; gpsp3d.mk prefixes it with this folder's path)
if [ -f "$HERE/../video.cc" ]; then CORE=..; else CORE=../gpsp; fi
cd "$HERE/$CORE"
# First build with the threaded renderer: shared headers changed, rebuild everything once
[ -f video_thread.o ] || make platform=ctr clean
make platform=ctr PROFILE=1 -j$JOBS
mkdir -p "$HERE/lib/lib"
cp gpsp_libretro_ctr.a "$HERE/lib/lib/libgpsp.a"

echo "=== 2/3 gpSP 3D app ==="
cd "$HERE"
# GPU shader (from mGBA): assemble and embed as uishader.c / uishader.h
mkdir -p gen
picasso -o gen/uishader.shbin -h gen/uishader.shbin.h res/uishader.v.pica res/uishader.g.pica
(cd gen && raw2c uishader.shbin)
make -f gpsp3d.mk CORE="$CORE" -j$JOBS

echo "=== 3/3 CIA ==="
mkdir -p build
bannertool makebanner -i res/banner.png -a res/silent.wav -o build/gpsp3d.bnr
bannertool makesmdh -s "gpSP 3D" -l "gpSP 3D - standalone GBA for 3DS" -p "gpSP team, bheppe" \
  -i res/icon.png -o build/gpsp3d.icn
makerom -f cia -o gpsp3d.cia -rsf res/app.rsf -exefslogo -elf gpsp3d.elf \
  -icon build/gpsp3d.icn -banner build/gpsp3d.bnr \
  -DAPP_TITLE="gpSP 3D" -DAPP_PRODUCT_CODE="CTR-GPSP3D" -DAPP_UNIQUE_ID=0xBAC81 \
  -DAPP_SYSTEM_MODE=64MB -DAPP_SYSTEM_MODE_EXT=124MB -DAPP_ENCRYPTED=false \
  -major 0 -minor 1 -micro 0

mkdir -p "$HERE/out"
cp gpsp3d.cia gpsp3d.3dsx "$HERE/out/"
echo
echo "DONE: $HERE/out/gpsp3d.cia"
