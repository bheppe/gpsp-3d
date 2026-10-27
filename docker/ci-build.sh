#!/bin/bash
# Builds gpSP 3D and Icon Maker, then checks a forwarder made from the real
# template CIA on the PC. What the workflows run in the build image; works the
# same in the devcontainer:
#   bash docker/ci-build.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

bash "$ROOT/gpsp3ds/build-gpsp3ds.sh"
bash "$ROOT/iconmaker/build-iconmaker.sh"

echo "=== Icon Maker forwarder test ==="
IM="$ROOT/iconmaker"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
gcc -O2 -I"$IM/source" "$IM/test/fwdtest.c" "$IM/test/sha256.c" \
  "$IM/source/fwdcia.c" "$IM/source/banner.c" "$IM/source/deficon.c" -lm -o "$TMP/fwdtest"
# fwdtest writes its banner input to the current folder
cd "$TMP"
./fwdtest "$IM/out/template-gpsp3d.cia" fwd.cia 000400000F3D1234 \
  "sdmc:/roms/gba/Test Game.gba" "Test Game"
python3 "$IM/test/verify.py" fwd.cia
