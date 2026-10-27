# Build image for gpSP 3D: devkitARM with the 3DS packages, plus bannertool and
# makerom for the CIAs. Used by .devcontainer/devcontainer.json; GitHub Actions
# can build in the same image. Everything is pinned (devkitARM tag, bannertool
# and makerom commits), so rebuilding it cannot silently change the toolchain.
ARG DEVKITARM_TAG=20260610

FROM docker.io/devkitpro/devkitarm:${DEVKITARM_TAG} AS tools-builder

ARG BANNERTOOL_REF=734d33be79fd3f8c29c6296158f06ac7c5ca9dcb
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

FROM docker.io/devkitpro/devkitarm:${DEVKITARM_TAG}

# 3ds-dev: libctru, citro3d, picasso, 3dsxtool, smdhtool, raw2c
RUN dkp-pacman -Sy --noconfirm 3ds-dev && dkp-pacman -Scc --noconfirm

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
