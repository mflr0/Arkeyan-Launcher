#!/bin/sh
# One job inside a container of tools/ci/run.sh: linux and sanitizers (build and checks, native architecture), appimage
# (the x86_64 AppImage, amd64), windows (the exe and its checks under Wine, amd64). The checkout is mounted read only at /repo; sources and the build live in the job's volume
# (/work), so rebuilds are incremental. Results go to /out. VERSION (vX.Y.Z) sets the version of a release build.
set -eu

job=${1:?usage: job.sh linux|sanitizers|appimage|windows}
mkdir -p /work/src /out
rsync -a --delete --exclude .git --exclude '/build*' --exclude /dist --exclude /.scratch --exclude /AppDir /repo/ /work/src/
cd /work/src
version=${VERSION:-}; version=${version#v}
scratch=/work/launcher-test
rm -rf "$scratch" "$scratch-env"
mkdir -p "$scratch-env"

case "$job" in
    linux)
        cmake -S . -B /work/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-15 \
            -DCMAKE_CXX_COMPILER=clang++-15 ${version:+"-DLAUNCHER_VERSION=$version"}
        cmake --build /work/build --target ArkeyanLauncher ArkeyanLauncherTest FakeArkeyanBuilder
        SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy /work/build/ArkeyanLauncherTest --no-disc "$scratch"
        ;;
    appimage)
        # The checks ran natively in the linux job: under amd64 emulation (Rosetta) posix_spawn does not report a missing
        # program, and llvmpipe's JIT crashes with the sanitizers.
        cmake -S . -B /work/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-15 \
            -DCMAKE_CXX_COMPILER=clang++-15 ${version:+"-DLAUNCHER_VERSION=$version"}
        cmake --build /work/build --target ArkeyanLauncher
        rm -rf /work/AppDir
        cmake --install /work/build --prefix /work/AppDir/usr
        rsvg-convert -w 256 -h 256 resources/icons/skylauncher.svg -o /work/skylauncher.png
        cd /work
        /opt/linuxdeploy --appimage-extract-and-run --appdir AppDir -e AppDir/usr/bin/ArkeyanLauncher \
            -d src/resources/linux/skylauncher.desktop -i skylauncher.png --output appimage
        mv Arkeyan*.AppImage /out/ArkeyanLauncher-x86_64.AppImage
        ;;
    sanitizers)
        cmake -S . -B /work/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSKY_SANITIZE=ON \
            -DCMAKE_C_COMPILER=clang-15 -DCMAKE_CXX_COMPILER=clang++-15
        cmake --build /work/build --target ArkeyanLauncher ArkeyanLauncherTest FakeArkeyanBuilder
        ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 SKY_LAUNCHER_TEST_TIMEOUT_MS=300000 \
            SKY_LAUNCHER_TEST_PADWALK_MINUTES=45 SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
            /work/build/ArkeyanLauncherTest --no-disc "$scratch"
        ;;
    windows)
        cmake -S . -B /work/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE=/work/src/tools/ci/clang-cl-xwin.cmake ${version:+"-DLAUNCHER_VERSION=$version"}
        cmake --build /work/build --target ArkeyanLauncher ArkeyanLauncherTest FakeArkeyanBuilder
        export WINEPREFIX=/work/wine
        [ -d "$WINEPREFIX/drive_c/windows/system32" ] || xvfb-run -a wineboot -i
        cp /opt/wincurl/curl.exe "$WINEPREFIX/drive_c/windows/system32/curl.exe"
        # Every folder and settings file in the job's scratch folder, no update check (no network); Z: is Wine's /.
        win=$(printf 'Z:%s' "$scratch-env" | tr / '\\')
        SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SKY_LAUNCHER_NO_UPDATE_CHECK=1 \
            SKY_SETTINGS="$win\\none.ini" SKY_LAUNCHER_INI="$win\\none.ini" SKY_COLLECTION="$win\\collection" \
            SKY_SAVE_DIR="$win\\save" SKYLANDERS_DATA="$win\\data" SKY_LAUNCHER_DATA="$win\\launcher-data" \
            SKY_LAUNCHER_DESKTOP="$win\\desktop" SKY_LOG="$win\\game.log"  \
            xvfb-run -a wine /work/build/ArkeyanLauncherTest.exe --no-disc "$(printf 'Z:%s' "$scratch" | tr / '\\')"
        cp /work/build/ArkeyanLauncher.exe /out/ArkeyanLauncher.exe
        ;;
    *) echo "error: unknown job $job" >&2; exit 2 ;;
esac
