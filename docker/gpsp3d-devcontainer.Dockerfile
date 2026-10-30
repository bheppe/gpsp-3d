# Build image for gpSP 3D: devkitARM with the 3DS packages, plus bannertool and
# makerom for the CIAs. Used by .devcontainer/devcontainer.json; GitHub Actions
# can build in the same image. The toolchain is pinned, so rebuilding the image
# cannot silently change it: devkitARM by digest (its image already has 3ds-dev:
# libctru, citro3d, picasso, 3dsxtool, smdhtool, raw2c, and 3ds-libjpeg-turbo
# for Icon Maker), bannertool and makerom
# by commit. Renovate (.github/renovate.json) proposes updates to all three;
# by hand, take a newer tag from https://hub.docker.com/r/devkitpro/devkitarm/tags
# and its index digest.
ARG DEVKITARM_IMAGE=docker.io/devkitpro/devkitarm:20260610@sha256:116afba8df8453961de2936ffab20dd441edf4d682856c1ec8b0e53d7ed0bbf5

FROM ${DEVKITARM_IMAGE} AS tools-builder

# renovate: datasource=git-refs depName=bannertool packageName=https://github.com/carstene1ns/3ds-bannertool branch=main
ARG BANNERTOOL_REF=734d33be79fd3f8c29c6296158f06ac7c5ca9dcb
# renovate: datasource=git-refs depName=makerom packageName=https://github.com/3DSGuy/Project_CTR branch=master
ARG PROJECT_CTR_REF=d52c2b0e788c2d085763d7c5dcc4b2edd9ffb7d8

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    build-essential \
    git \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/carstene1ns/3ds-bannertool.git /tmp/bannertool \
    && git -C /tmp/bannertool checkout "${BANNERTOOL_REF}" \
    && cmake -DCMAKE_BUILD_TYPE=Release -B /tmp/bannertool/build /tmp/bannertool \
    && cmake --build /tmp/bannertool/build -j$(nproc) \
    && install -m755 /tmp/bannertool/build/bannertool /usr/local/bin/bannertool

RUN git clone https://github.com/3DSGuy/Project_CTR.git /tmp/Project_CTR \
    && git -C /tmp/Project_CTR checkout "${PROJECT_CTR_REF}" \
    && make -C /tmp/Project_CTR/makerom deps \
    && make -C /tmp/Project_CTR/makerom -j$(nproc) \
    && install -m755 /tmp/Project_CTR/makerom/bin/makerom /usr/local/bin/makerom

FROM ${DEVKITARM_IMAGE}

# Icon Maker decodes JPEGs with libjpeg-turbo; the pinned image has it, this
# makes sure (and installs nothing then)
RUN dkp-pacman -S --needed --noconfirm 3ds-libjpeg-turbo

# Host tools: a native compiler and Python for the PC tests (iconmaker/test) and
# gpsp3ds/tools/make_font.py, plus git, gh and ssh for working in the container.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    python3 \
    python3-pil \
    git \
    gh \
    openssh-client \
    && rm -rf /var/lib/apt/lists/*

COPY --from=tools-builder /usr/local/bin/bannertool /usr/local/bin/bannertool
COPY --from=tools-builder /usr/local/bin/makerom /usr/local/bin/makerom

ENV DEVKITPRO=/opt/devkitpro \
    DEVKITARM=/opt/devkitpro/devkitARM \
    PATH=/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:${PATH}
