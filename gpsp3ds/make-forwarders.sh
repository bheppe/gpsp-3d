#!/bin/bash
# Makes a HOME Menu icon (a small "forwarder" CIA) for every GBA game in a
# folder. Each one starts the installed gpSP 3D straight into its game.
#
# Run in the devkitPro shell (MSYS2 on Windows):
#   bash make-forwarders.sh [--fit pad|crop] [--pad-color COLOR] <folder with the games on this PC> <same folder on the 3DS SD card>
# Example (SD card in the PC as drive E:, games in E:\roms\gba):
#   bash make-forwarders.sh /e/roms/gba /roms/gba
#
# Icons: put a picture named like the game next to it (Pokemon Emerald.gba ->
# Pokemon Emerald.png, .jpg or .jpeg) for a custom icon; otherwise the gpSP 3D
# icon is used. Banners (the picture on the top screen when the icon is
# selected): a picture named like the game plus ".banner"
# (Pokemon Emerald.banner.png, .jpg or .jpeg), and optionally a short WAV for
# its sound (Pokemon Emerald.banner.wav); otherwise the gpSP 3D banner, silent.
#
# With ImageMagick (pacman -S mingw-w64-x86_64-imagemagick), pictures can be
# any size and JPEGs too. One of another shape than the icon (48x48) or the
# banner (256x128) is
#   --fit pad   shrunk to fit, with bars (the default), or
#   --fit crop  scaled to fill, with what sticks out cut off.
# The bars are see-through on the banner and white on the icon (icons can't
# be see-through), or --pad-color COLOR (black, '#1e2327', ...) on both.
# Transparent parts of an icon are white; a banner keeps them.
# Without ImageMagick, only PNGs of the exact size can be used.
# The CIAs go to out/forwarders/. Install them with FBI. gpSP 3D itself must be
# installed as a CIA.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
USAGE="usage: make-forwarders.sh [--fit pad|crop] [--pad-color COLOR] <games folder on this PC> <same folder on the SD card>"
FIT=pad
PAD_COLOR=none
args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --fit) [ $# -ge 2 ] || { echo "--fit needs pad or crop"; echo "$USAGE"; exit 1; }; FIT="$2"; shift ;;
    --fit=*) FIT="${1#--fit=}" ;;
    --pad-color) [ $# -ge 2 ] || { echo "--pad-color needs a color"; echo "$USAGE"; exit 1; }; PAD_COLOR="$2"; shift ;;
    --pad-color=*) PAD_COLOR="${1#--pad-color=}" ;;
    -h|--help) echo "$USAGE"; exit 0 ;;
    --) shift; args+=("$@"); break ;;
    -*) echo "Unknown option: $1"; echo "$USAGE"; exit 1 ;;
    *) args+=("$1") ;;
  esac
  shift
done
set -- "${args[@]}"
case "$FIT" in pad|crop) ;; *) echo "--fit must be pad or crop, not '$FIT'"; exit 1 ;; esac
PC_DIR="${1:?$USAGE}"
SD_DIR="${2:?$USAGE}"
SD_DIR="/${SD_DIR#/}"; SD_DIR="${SD_DIR%/}"

export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
export DEVKITARM=${DEVKITARM:-$DEVKITPRO/devkitARM}
export CTRULIB=${CTRULIB:-$DEVKITPRO/libctru}
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
for t in bannertool makerom; do
  command -v $t >/dev/null || { echo "MISSING: $t (check devkitPro / tools/bin)"; exit 1; }
done
# ImageMagick 7's magick (not convert: on Windows that's a system tool)
MAGICK=
if command -v magick >/dev/null; then
  MAGICK=magick
  if [ "$FIT" = pad ] && [ "$PAD_COLOR" != none ] && [ -n "$(magick xc:"$PAD_COLOR" null: 2>&1)" ]; then
    echo "--pad-color: unknown color '$PAD_COLOR'"; exit 1
  fi
else
  echo "(ImageMagick not found: custom icons/banners must be PNGs of the exact size; --fit has no effect)"
fi

# the picture <base>.png/.jpg/.jpeg in any case (no glob: names have [ ])
find_img() {
  local ext
  for ext in png PNG Png jpg JPG Jpg jpeg JPEG Jpeg; do
    [ -f "$1.$ext" ] && { printf '%s' "$1.$ext"; return 0; }
  done
  return 1
}
# <base> <WxH> <out.png> <icon|banner>: prints the picture to use, or fails
# (no picture, or one that can't be used: then with a note)
prepare_img() {
  local src fill flat=() bars="$PAD_COLOR"
  src="$(find_img "$1")" || return 1
  if [ -z "$MAGICK" ]; then
    case "$src" in
      *.[pP][nN][gG]) printf '%s' "$src"; return 0 ;;
      *) echo "  ($(basename "$src"): JPEG needs ImageMagick, not used)" >&2; return 1 ;;
    esac
  fi
  # icons have no transparency: on white (the bars too, unless --pad-color);
  # a banner keeps it (Copy: only the bars get the pad color)
  if [ "$4" = icon ]; then
    flat=(-background white -alpha remove -alpha off)
    [ "$bars" = none ] && bars=white
  fi
  if [ "$FIT" = crop ]; then
    fill=(-resize "$2^" -gravity center -compose Copy -extent "$2")
  else
    fill=(-resize "$2" -background "$bars" -gravity center -compose Copy -extent "$2")
  fi
  if magick "${src}[0]" "${flat[@]}" "${fill[@]}" "png32:$3" 2>/dev/null; then
    printf '%s' "$3"
  else
    echo "  ($(basename "$src"): can't be read, not used)" >&2
    return 1
  fi
}

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
  img="$(prepare_img "$PC_DIR/$name" 48x48 "$TMP/icon.png" icon)" && icon="$img"
  banner="$TMP/default.bnr"
  if img="$(prepare_img "$PC_DIR/$name.banner" 256x128 "$TMP/banner.png" banner)"; then
    sound="$HERE/res/silent.wav"
    [ -f "$PC_DIR/$name.banner.wav" ] && sound="$PC_DIR/$name.banner.wav"
    if bannertool makebanner -i "$img" -a "$sound" -o "$TMP/game.bnr" >/dev/null 2>&1; then
      banner="$TMP/game.bnr"
    else
      echo "  ($(basename "$(find_img "$PC_DIR/$name.banner")"): not used, it must be a 256x128 PNG; the WAV a short 16-bit one)"
    fi
  fi
  rm -rf "$TMP/romfs"; mkdir -p "$TMP/romfs"
  printf '%s\n' "$sdpath" > "$TMP/romfs/path.txt"
  if ! bannertool makesmdh -s "${name:0:63}" -l "${name:0:63} (gpSP 3D)" -p "gpSP 3D" \
      -i "$icon" -o "$TMP/icon.icn" >/dev/null 2>&1; then
    echo "  ($(basename "$icon"): icon not used, it must be a 48x48 PNG)"
    bannertool makesmdh -s "${name:0:63}" -l "${name:0:63} (gpSP 3D)" -p "gpSP 3D" \
      -i "$HERE/res/icon.png" -o "$TMP/icon.icn" >/dev/null
  fi
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
