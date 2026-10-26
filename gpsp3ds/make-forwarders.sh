#!/bin/bash
# Makes a HOME Menu icon (a small "forwarder" CIA) for every GBA game in a
# folder. Each one starts the installed gpSP 3D straight into its game.
#
# Run in the devkitPro shell (MSYS2 on Windows):
#   bash make-forwarders.sh <folder with the games on this PC> <same folder on the 3DS SD card>
# Example (SD card in the PC as drive E:, games in E:\roms\gba):
#   bash make-forwarders.sh /e/roms/gba /roms/gba
#
# Icons: put a 48x48 PNG named like the game next to it (Pokemon Emerald.gba ->
# Pokemon Emerald.png) for a custom icon; otherwise the gpSP 3D icon is used.
# Banners (the picture on the top screen when the icon is selected): a 256x128
# PNG named like the game plus ".banner" (Pokemon Emerald.banner.png), and
# optionally a short WAV for its sound (Pokemon Emerald.banner.wav); otherwise
# the gpSP 3D banner, silent.
# The CIAs go to out/forwarders/. Install them with FBI. gpSP 3D itself must be
# installed as a CIA.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
PC_DIR="${1:?usage: make-forwarders.sh <games folder on this PC> <same folder on the SD card>}"
SD_DIR="${2:?usage: make-forwarders.sh <games folder on this PC> <same folder on the SD card>}"
SD_DIR="/${SD_DIR#/}"; SD_DIR="${SD_DIR%/}"

export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export CTRULIB=${CTRULIB:-$DEVKITPRO/libctru}
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
for t in bannertool makerom; do
  command -v $t >/dev/null || { echo "MISSING: $t (check devkitPro / tools/bin)"; exit 1; }
done

# the forwarder program (built once)
make -C "$HERE/forwarder" >/dev/null
ELF="$HERE/forwarder/forwarder.elf"

OUT="$HERE/out/forwarders"; TMP="$HERE/build/fwd"
mkdir -p "$OUT" "$TMP"
bannertool makebanner -i "$HERE/res/banner.png" -a "$HERE/res/silent.wav" -o "$TMP/default.bnr" >/dev/null

n=0
shopt -s nullglob nocaseglob
for rom in "$PC_DIR"/*.gba "$PC_DIR"/*.zip; do
  case "$rom" in *.banner.*) continue ;; esac
  file="$(basename "$rom")"
  name="${file%.*}"
  sdpath="sdmc:$SD_DIR/$file"
  # unique title ID per game path (0xC0000-0xEFFFF, away from gpSP 3D's own)
  crc=$(printf '%s' "$sdpath" | cksum | cut -d' ' -f1)
  uid=$(printf '0x%05X' $(( 0xC0000 + crc % 0x30000 )))
  icon="$HERE/res/icon.png"
  [ -f "$PC_DIR/$name.png" ] && icon="$PC_DIR/$name.png"
  banner="$TMP/default.bnr"
  if [ -f "$PC_DIR/$name.banner.png" ]; then
    sound="$HERE/res/silent.wav"
    [ -f "$PC_DIR/$name.banner.wav" ] && sound="$PC_DIR/$name.banner.wav"
    if bannertool makebanner -i "$PC_DIR/$name.banner.png" -a "$sound" -o "$TMP/game.bnr" >/dev/null; then
      banner="$TMP/game.bnr"
    else
      echo "  ($name.banner.png: not used, it must be a 256x128 PNG; the WAV a short 16-bit one)"
    fi
  fi
  rm -rf "$TMP/romfs"; mkdir -p "$TMP/romfs"
  printf '%s\n' "$sdpath" > "$TMP/romfs/path.txt"
  bannertool makesmdh -s "${name:0:63}" -l "${name:0:63} (gpSP 3D)" -p "gpSP 3D" \
    -i "$icon" -o "$TMP/icon.icn" >/dev/null
  safe="$(printf '%s' "$name" | tr -c 'A-Za-z0-9 ._()-' '_')"
  makerom -f cia -o "$OUT/$safe.cia" -rsf "$HERE/forwarder/forwarder.rsf" -exefslogo -elf "$ELF" \
    -icon "$TMP/icon.icn" -banner "$banner" \
    -DAPP_TITLE="gp3dfwd" -DAPP_PRODUCT_CODE="CTR-GP3F" -DAPP_UNIQUE_ID=$uid \
    -DAPP_SYSTEM_MODE=64MB -DAPP_SYSTEM_MODE_EXT=Legacy -DAPP_ENCRYPTED=false \
    -DAPP_ROMFS="$TMP/romfs" >/dev/null
  echo "  $name  ->  out/forwarders/$safe.cia  ($sdpath)"
  n=$((n + 1))
done
echo
echo "Made $n forwarder(s) in $OUT"
[ $n -gt 0 ] || echo "No .gba or .zip files found in $PC_DIR"
