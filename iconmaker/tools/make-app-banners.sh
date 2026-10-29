#!/bin/bash
# Remakes the 3D HOME Menu banners of gpSP 3D and Icon Maker from their
# pictures (res/banner3d-back.png 256x128, res/banner3d-label.png 128x128)
# and the jingle, with the same 3D model Icon Maker uses for games.
set -e
IM="$(cd "$(dirname "$0")/.." && pwd)"
GP="$IM/../gpsp3ds"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
gcc -O2 -I"$IM/source" "$IM/tools/app_banner3d.c" -lm -o "$TMP/app_banner3d"
"$TMP/app_banner3d" "$IM/res/banner3d.cgfx" "$GP/res/banner3d-back.png" "$GP/res/banner3d-label.png" \
  "$IM/res/jingle.bcwav" "$GP/res/banner3d.bnr"
"$TMP/app_banner3d" "$IM/res/banner3d.cgfx" "$IM/res/banner3d-back.png" "$IM/res/banner3d-label.png" \
  "$IM/res/jingle.bcwav" "$IM/res/banner3d-app.bnr"
