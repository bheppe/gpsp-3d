#!/bin/bash
# Builds gpSP 3D. What the workflows run in the build image; works the same
# in the devcontainer:
#   bash docker/ci-build.sh
# (Icon Maker has its own repo and CI: https://github.com/bheppe/iconmaker-3ds)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# releases need libcurl for the in-app update check (the 3DS's own HTTPS can't reach GitHub)
[ -f "${DEVKITPRO:-/opt/devkitpro}/portlibs/3ds/lib/libcurl.a" ] || { echo "MISSING: 3ds-curl (dkp-pacman -S 3ds-curl)"; exit 1; }
bash "$ROOT/gpsp3ds/build-gpsp3ds.sh"
