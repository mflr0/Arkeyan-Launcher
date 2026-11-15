#!/usr/bin/env bash
# The project's checks, run on a Mac (CONTRIBUTING.md, "Checks"): the format, a pull request's title and sign-offs, the
# macOS build and tests (native), and in Docker containers the Linux build and the sanitizer build (native
# architecture), the AppImage (amd64, with --dist) and the Windows build (clang-cl, amd64) with its tests under Wine.
#
#   tools/ci/run.sh [--quick] [--only job,job] [--root DIR] [--dist DIR]
#
# Jobs: format, pr (when TITLE, BASE and HEAD are set: tools/check_pull_request.sh), macos, linux, sanitizers, windows,
# appimage (added to linux by --dist).
# --quick runs format and macos (the pre-push hook). --root checks another checkout (tools/ci/check_pr.sh). --dist
# copies the release files (ArkeyanLauncher.exe, ArkeyanLauncher-x86_64.AppImage, ArkeyanLauncher-macOS.dmg and .zip)
# there. VERSION=vX.Y.Z builds that version (tools/release_local.sh). Logs: <root>/.scratch/ci/<job>.log.
# Needs Docker, CMake, Ninja and rsvg-convert (brew install ninja librsvg). Exit code 0: every job passed.
set -uo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
jobs=""; dist=""
while [ $# -gt 0 ]; do
    case $1 in
        --quick) jobs=format,macos ;;
        --only) shift; jobs=${1:?--only needs a list of jobs} ;;
        --root) shift; root=$(cd "${1:?--root needs a folder}" && pwd) ;;
        --dist) shift; mkdir -p "${1:?--dist needs a folder}"; dist=$(cd "$1" && pwd) ;;
        *) echo "usage: tools/ci/run.sh [--quick] [--only job,job] [--root DIR] [--dist DIR]" >&2; exit 2 ;;
    esac
    shift
done
[ -n "$jobs" ] || jobs=format,pr,macos,linux,sanitizers,windows
# The AppImage is a release file: built with --dist.
case ",$jobs," in *,linux,*) [ -z "$dist" ] || jobs+=,appimage ;; esac
here=$(cd "$(dirname "$0")" && pwd)
renice -n 10 $$ >/dev/null
logs=$root/.scratch/ci
mkdir -p "$logs"
export VERSION=${VERSION:-} SKY_LAUNCHER_TEST_I18N_MISSING_OK=${SKY_LAUNCHER_TEST_I18N_MISSING_OK:-0}
# The checks run natively (no emulation); the release files are x86_64.
case $(uname -m) in arm64 | aarch64) native=linux/arm64 ;; *) native=linux/amd64 ;; esac
# One volume per checkout and job keeps each build incremental.
key=$(printf '%s' "$root" | shasum | cut -c1-8)

# image <Dockerfile name> <platform>: built locally, never pushed.
image() {
    docker build -q --platform "$2" -t "arkeyan-launcher-$1-${2#linux/}" -f "$here/$1.Dockerfile" "$here" >/dev/null
}

container() { # job image platform
    image "$2" "$3" || return 1
    local out; out=$(mktemp -d)
    docker run --rm --platform "$3" -e VERSION -e SKY_LAUNCHER_TEST_I18N_MISSING_OK -v "$root:/repo:ro" \
        -v "arkeyan-launcher-$key-$1:/work" -v "$out:/out" "arkeyan-launcher-$2-${3#linux/}" /repo/tools/ci/job.sh "$1" ||
        return 1
    if [ -n "$dist" ]; then cp "$out"/* "$dist"/ 2>/dev/null || true; fi
    rm -rf "$out"
}

macos() {
    local build=$root/build-ci-macos scratch; scratch=$(mktemp -d)
    # The build paths stay out of the binaries (no home folder in a release file).
    cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 "-DCMAKE_C_FLAGS=-ffile-prefix-map=$root=." \
        "-DCMAKE_CXX_FLAGS=-ffile-prefix-map=$root=." "-DCMAKE_OBJCXX_FLAGS=-ffile-prefix-map=$root=." \
        ${VERSION:+"-DLAUNCHER_VERSION=${VERSION#v}"} || return 1
    cmake --build "$build" --target ArkeyanLauncher ArkeyanLauncherTest FakeArkeyanBuilder || return 1
    # Every folder and settings file in the scratch folder: never the player's own.
    mkdir -p "$scratch/env"
    SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy SKY_LAUNCHER_NO_UPDATE_CHECK=1 \
        SKY_SETTINGS="$scratch/env/none.ini" SKY_LAUNCHER_INI="$scratch/env/none.ini" \
        SKY_COLLECTION="$scratch/env/collection" SKY_SAVE_DIR="$scratch/env/save" SKYLANDERS_DATA="$scratch/env/data" \
        SKY_LAUNCHER_DATA="$scratch/env/launcher-data" SKY_LAUNCHER_DESKTOP="$scratch/env/desktop" \
        SKY_LOG="$scratch/env/game.log" "$build/ArkeyanLauncherTest" --no-disc "$scratch/test" || return 1
    rm -rf "$scratch"
    if [ -n "$dist" ]; then
        "$root/tools/package_macos.sh" "$build/ArkeyanLauncher.app" "$root/resources/icons/skylauncher.svg" "$dist"
    fi
}

format() {
    image linux "$native" || return 1
    (cd "$root" && tools/check_format.sh --list) |
        docker run --rm -i --platform "$native" -v "$root:/repo:ro" -w /repo "arkeyan-launcher-linux-${native#linux/}" \
            sh -c 'xargs clang-format --dry-run --Werror --style=file' || return 1
    echo "format ok"
}

pr() {
    if [ -z "${TITLE:-}" ] || [ -z "${BASE:-}" ] || [ -z "${HEAD:-}" ]; then
        echo "no pull request (TITLE, BASE, HEAD unset): skipped"
        return 0
    fi
    (cd "$root" && tools/check_pull_request.sh)
}

failed=0
summary=""
for job in ${jobs//,/ }; do
    case $job in
        format | pr | macos) run=$job ;;
        linux | sanitizers) run="container $job linux $native" ;;
        appimage) run="container appimage linux linux/amd64" ;;
        windows) run="container windows windows linux/amd64" ;;
        *) echo "error: unknown job $job" >&2; exit 2 ;;
    esac
    start=$SECONDS
    printf '%-11s ' "$job"
    if $run >"$logs/$job.log" 2>&1; then result=PASS; else result=FAIL; failed=1; fi
    line=$(printf '%-11s %s (%ss)' "$job" "$result" $((SECONDS - start)))
    echo "$result ($((SECONDS - start))s)"
    [ $result = PASS ] || tail -20 "$logs/$job.log" | sed 's/^/    /'
    summary+="$line"$'\n'
done
printf '%s' "$summary" >"$logs/summary.txt"
[ $failed -eq 0 ] && echo "all jobs passed" || echo "failed: see $logs/<job>.log"
exit $failed
