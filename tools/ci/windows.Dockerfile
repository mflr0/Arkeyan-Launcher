# Cross-builds the Windows launcher with clang-cl and lld-link (the MSVC ABI, as a Windows build with clang-cl) and runs
# its no-disc checks under Wine. xwin downloads Microsoft's CRT and SDK at build time: build this image locally, never
# push it (the Microsoft licence covers using them to build, not redistributing them). amd64 only (Wine runs the exe).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ARG LLVM=19
ARG XWIN_VERSION=0.10.0
ARG XWIN_SHA256=d870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88
# Windows 10 and later ship curl.exe in System32 (the launcher's downloads use it); Wine does not: curl's own build.
ARG CURL_VERSION=8.23.0_9
ARG CURL_SHA256=89cb71930f4242d884e8e349463455932ab1d089b20f4f99318261a3cbff9167

RUN dpkg --add-architecture i386 && apt-get update && \
    apt-get install -y --no-install-recommends \
        clang-$LLVM lld-$LLVM llvm-$LLVM cmake ninja-build git python3 perl rsync curl ca-certificates \
        wine wine64 xvfb xauth unzip && \
    rm -rf /var/lib/apt/lists/* && \
    # clang runs as clang-cl when called by that name.
    ln -sf /usr/lib/llvm-$LLVM/bin/clang /usr/local/bin/clang-cl && \
    for tool in lld-link llvm-rc llvm-mt llvm-lib; do ln -sf /usr/bin/$tool-$LLVM /usr/local/bin/$tool; done && \
    curl -fsSL -o /tmp/xwin.tar.gz \
        "https://github.com/Jake-Shadle/xwin/releases/download/$XWIN_VERSION/xwin-$XWIN_VERSION-x86_64-unknown-linux-musl.tar.gz" && \
    echo "$XWIN_SHA256  /tmp/xwin.tar.gz" | sha256sum -c - && \
    tar -xzf /tmp/xwin.tar.gz -C /tmp && \
    /tmp/xwin-$XWIN_VERSION-x86_64-unknown-linux-musl/xwin --accept-license --arch x86_64 --variant desktop \
        splat --output /opt/xwin && \
    rm -rf /tmp/xwin* /root/.xwin-cache && \
    curl -fsSL -o /tmp/curl.zip "https://curl.se/windows/dl-$CURL_VERSION/curl-$CURL_VERSION-win64-mingw.zip" && \
    echo "$CURL_SHA256  /tmp/curl.zip" | sha256sum -c - && \
    mkdir -p /opt/wincurl && unzip -j -q /tmp/curl.zip "*/bin/curl.exe" -d /opt/wincurl && rm /tmp/curl.zip

# The jobs run as an ordinary user, as on a player's machine: root passes the permission checks the tests expect to fail.
RUN useradd -m ci && mkdir -p /work && chown ci /work
USER ci
ENV HOME=/tmp LANG=C.UTF-8 LC_ALL=C.UTF-8 WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml=
WORKDIR /work
