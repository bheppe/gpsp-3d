#!/bin/bash
# Prints the build image for this checkout: the published devcontainer image,
# tagged with a hash of its Dockerfile. So every commit uses exactly the image
# its own Dockerfile describes, never a newer or older one.
# (The Dockerfile copies nothing from the repo, so it alone defines the image.)
#   bash docker/build-image.sh             -> ghcr.io/<owner>/gpsp3d-devcontainer:<hash>
#   bash docker/build-image.sh --pull      -> also makes it available locally:
#                                             pulled, or built if not published yet
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOCKERFILE="$ROOT/docker/gpsp3d-devcontainer.Dockerfile"
OWNER="${GITHUB_REPOSITORY_OWNER:-bheppe}"
IMAGE="ghcr.io/${OWNER,,}/gpsp3d-devcontainer:$(sha256sum "$DOCKERFILE" | cut -c1-12)"

if [ "$1" = --pull ]; then
  docker pull "$IMAGE" >&2 || {
    echo "$IMAGE is not published (yet): building it" >&2
    docker build -f "$DOCKERFILE" -t "$IMAGE" "$ROOT" >&2
  }
fi
echo "$IMAGE"
