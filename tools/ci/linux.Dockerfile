# Builds and tests the Linux launcher, the sanitizer build and the format check (tools/ci/run.sh), natively on the host's
# architecture, and the x86_64 AppImage in an amd64 container. 22.04: the AppImage needs the glibc it was built against
# (2.35), and the game kit's baseline is 2.34.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
# linuxdeploy's continuous build as checked on 2026-11-02: a newer one stops here until it is checked and this hash updated.
ARG LINUXDEPLOY_SHA256=df908b19f1ed1cdafa79508d415aa12d6c56ce4d56d4149aa8c09286718706e5
# The repository's one clang-format version (CONTRIBUTING.md).
ARG CLANG_FORMAT=23.1.4

ARG TARGETARCH
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ninja-build cmake pkg-config clang-15 libstdc++-12-dev git rsync curl ca-certificates file python3-pip \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
        libxkbcommon-dev libwayland-dev wayland-protocols libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev \
        libdbus-1-dev libudev-dev libdrm-dev libgbm-dev librsvg2-bin libfuse2 && \
    rm -rf /var/lib/apt/lists/* && \
    pip3 install --no-cache-dir clang-format==$CLANG_FORMAT && \
    if [ "$TARGETARCH" = amd64 ]; then \
    curl -fsSL -o /opt/linuxdeploy \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage && \
    echo "$LINUXDEPLOY_SHA256  /opt/linuxdeploy" | sha256sum -c - && \
    chmod +x /opt/linuxdeploy && \
    # The AppImage magic at offset 8 of the ELF header keeps x86_64 emulation (Rosetta, qemu binfmt) from running it.
    printf '\0\0\0' | dd of=/opt/linuxdeploy bs=1 seek=8 count=3 conv=notrunc 2>/dev/null; fi

# The configure step runs git apply in the copied submodules.
RUN git config --system --add safe.directory '*'
# The jobs run as an ordinary user, as on a player's machine: root passes the permission checks the tests expect to fail.
RUN useradd -m ci && mkdir -p /work && chown ci /work
USER ci
ENV HOME=/tmp LANG=C.UTF-8 LC_ALL=C.UTF-8
WORKDIR /work
