#!/bin/bash
# Builds Icon Maker for gpSP 3D (run in the devkitPro MSYS2 shell):
#   1. the template forwarder CIA it carries (data/fwd_gpsp3d.bin),
#   2. the app: out/iconmaker.3dsx and out/iconmaker.cia.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
GP="$HERE/../gpsp3ds"
cd "$HERE"
# Everything is also written to build.log
exec > >(tee "$HERE/build.log") 2>&1
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export CTRULIB=${CTRULIB:-$DEVKITPRO/libctru}
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
for t in arm-none-eabi-gcc bannertool makerom; do
  command -v $t >/dev/null || { echo "MISSING: $t (check devkitPro / tools/bin)"; exit 1; }
done
TMP="$HERE/build-template"
rm -rf "$TMP"; mkdir -p "$TMP/romfs" data out

echo "=== 1/2 template forwarder ==="
make -C "$GP/forwarder" >/dev/null
# the forwarder ignores romfs:/path.txt once Icon Maker fills in its slot
printf '\n' > "$TMP/romfs/path.txt"
# the banner picture is noise: it hardly compresses, so it leaves room for any
# picture Icon Maker puts in its place (every icon gets its own banner)
bannertool makebanner -i res/template-banner.png -a "$GP/res/silent.wav" -o "$TMP/default.bnr" >/dev/null
bannertool makesmdh -s "gpSP 3D game" -l "gpSP 3D game" -p "gpSP 3D" \
  -i "$GP/res/icon.png" -o "$TMP/icon.icn" >/dev/null
# unique ID 0xF3D00 = title ID 000400000F3D0000, which Icon Maker replaces;
# unencrypted and uncompressed so it can be patched on the 3DS
makerom -f cia -o data/fwd_gpsp3d.bin -rsf "$GP/forwarder/forwarder.rsf" -exefslogo \
  -elf "$GP/forwarder/forwarder.elf" -icon "$TMP/icon.icn" -banner "$TMP/default.bnr" \
  -DAPP_TITLE="gp3dfwd" -DAPP_PRODUCT_CODE="CTR-GP3F" -DAPP_UNIQUE_ID=0xF3D00 \
  -DAPP_SYSTEM_MODE=64MB -DAPP_SYSTEM_MODE_EXT=Legacy -DAPP_ENCRYPTED=false \
  -DAPP_ROMFS="$TMP/romfs"
cp data/fwd_gpsp3d.bin out/template-gpsp3d.cia
ls -l data/fwd_gpsp3d.bin

echo "=== 2/2 Icon Maker ==="
make
bannertool makebanner -i res/banner.png -a "$GP/res/silent.wav" -o "$TMP/app.bnr" >/dev/null
bannertool makesmdh -s "Icon Maker" -l "Icon Maker for gpSP 3D" -p "gpSP 3D" \
  -i res/icon.png -o "$TMP/app.icn" >/dev/null
makerom -f cia -o out/iconmaker.cia -rsf "$GP/res/app.rsf" -exefslogo -elf iconmaker.elf \
  -icon "$TMP/app.icn" -banner "$TMP/app.bnr" \
  -DAPP_TITLE="Icon Maker" -DAPP_PRODUCT_CODE="CTR-GP3I" -DAPP_UNIQUE_ID=0xF3D01 \
  -DAPP_SYSTEM_MODE=64MB -DAPP_SYSTEM_MODE_EXT=Legacy -DAPP_ENCRYPTED=false
cp iconmaker.3dsx out/
echo "Done: out/iconmaker.cia, out/iconmaker.3dsx (and out/template-gpsp3d.cia to check)"
