#!/usr/bin/env bash
# The project's checks, run on a Mac (CONTRIBUTING.md, "Checks"): the format, a pull request's title and sign-offs, the
# macOS build and tests (native), and in Docker containers the Linux build and the sanitizer build (native
# architecture), the AppImage (amd64, with --dist) and the Windows build (clang-cl, amd64) with its tests under Wine.
#
#   tools/ci/run.sh [--quick] [--only job,job] [--root DIR] [--dist DIR] [--serial] [--force]
#
# Jobs: format, pr (when TITLE, BASE and HEAD are set: tools/check_pull_request.sh), macos, linux, sanitizers, windows,
# appimage (added to linux by --dist), run at the same time (--serial: one after the other). A job that passed on the
# same tree (commits, uncommitted and untracked files but the docs, submodule commits, VERSION) is skipped, except pr
# and with --dist; --force runs it again.
# --quick runs format and macos (the pre-push hook). --root checks another checkout (tools/ci/check_pr.sh). --dist
# copies the release files (ArkeyanLauncher.exe, ArkeyanLauncher-x86_64.AppImage, ArkeyanLauncher-macOS.dmg and .zip)
# there. VERSION=vX.Y.Z builds that version (tools/release_local.sh). Logs: <root>/.scratch/ci/<job>.log.
# Needs Docker, CMake, Ninja and rsvg-convert (brew install ninja librsvg). Exit code 0: every job passed.
set -uo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
jobs=""; dist=""; serial=0; force=0
while [ $# -gt 0 ]; do
    case $1 in
        --quick) jobs=format,macos ;;
        --only) shift; jobs=${1:?--only needs a list of jobs} ;;
        --root) shift; root=$(cd "${1:?--root needs a folder}" && pwd) ;;
        --dist) shift; mkdir -p "${1:?--dist needs a folder}"; dist=$(cd "$1" && pwd) ;;
        --serial) serial=1 ;;
        --force) force=1 ;;
        *) echo "usage: tools/ci/run.sh [--quick] [--only job,job] [--root DIR] [--dist DIR] [--serial] [--force]" >&2
            exit 2 ;;
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
# files pathspec...: the tracked and untracked files those match, with their contents.
files() {
    git ls-files -s -- "$@"
    git diff HEAD --binary -- "$@"
    git ls-files --others --exclude-standard -- "$@"
    git ls-files -z --others --exclude-standard -- "$@" | xargs -0 git hash-object --
}
# What the jobs check; a job that passed on the same state is skipped. The docs change no result: *.md is left out,
# except the notices the builds ship.
state=$(cd "$root" && {
    files . ':(exclude,glob)**/*.md'
    files ':(glob)**/THIRD_PARTY_NOTICES.md'
    git submodule status
    echo "$VERSION $SKY_LAUNCHER_TEST_I18N_MISSING_OK"
} | shasum | cut -c1-40)
# One volume per checkout and job keeps each build incremental.
key=$(printf '%s' "$root" | shasum | cut -c1-8)

# image <Dockerfile name> <platform>: built locally, never pushed.
image() {
    docker build -q --platform "$2" -t "arkeyan-launcher-$1-${2#linux/}" -f "$here/$1.Dockerfile" "$here" >/dev/null
}

container() { # job image platform
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

# Parallel jobs would build a shared image at the same time: each is built once, first.
images=""
for job in ${jobs//,/ }; do
    case $job in
        format | linux | sanitizers) images+=" linux:$native" ;;
        appimage) images+=" linux:linux/amd64" ;;
        windows) images+=" windows:linux/amd64" ;;
    esac
done
for spec in $(printf '%s\n' $images | sort -u); do
    image "${spec%%:*}" "${spec#*:}" >"$logs/image.log" 2>&1 ||
        { echo "error: image ${spec%%:*} failed: see $logs/image.log" >&2; exit 1; }
done
# run_job job command: the log, the result and, on a pass, the state it passed on.
run_job() {
    local start=$SECONDS result
    if $2 >"$logs/$1.log" 2>&1; then
        echo "$state" >"$logs/$1.pass"; result=PASS
    else
        rm -f "$logs/$1.pass"; result=FAIL
    fi
    echo "$result $((SECONDS - start))" >"$logs/$1.result"
}

# Background jobs ignore Ctrl-C in a script: stop the whole process group.
trap 'trap - TERM; kill -TERM 0; exit 130' INT TERM
names=(); runs=(); pids=()
for job in ${jobs//,/ }; do
    case $job in
        format | pr | macos) run=$job ;;
        linux | sanitizers) run="container $job linux $native" ;;
        appimage) run="container appimage linux linux/amd64" ;;
        windows) run="container windows windows linux/amd64" ;;
        *) echo "error: unknown job $job" >&2; exit 2 ;;
    esac
    rm -f "$logs/$job.result" "$logs/$job.parallel.log"
    names+=("$job"); runs+=("$run")
    if [ $force -eq 0 ] && [ -z "$dist" ] && [ "$job" != pr ] &&
        [ "$(cat "$logs/$job.pass" 2>/dev/null)" = "$state" ]; then
        echo "SKIP 0" >"$logs/$job.result"
        pids+=("")
        continue
    fi
    run_job "$job" "$run" &
    pids+=($!)
    [ $serial -eq 0 ] || wait $!
done

failed=0
summary=""
again=()
for i in "${!names[@]}"; do
    job=${names[$i]}
    [ -z "${pids[$i]}" ] || wait "${pids[$i]}"
    read -r result took <"$logs/$job.result" || { result=FAIL; took=0; }
    case $result in
        SKIP) line=$(printf '%-11s SKIP (passed on this tree)' "$job") ;;
        *) line=$(printf '%-11s %s (%ss)' "$job" "$result" "$took") ;;
    esac
    echo "$line"
    summary+="$line"$'\n'
    [ "$result" != FAIL ] && continue
    if [ $serial -eq 0 ]; then again+=("$i"); else failed=1; tail -20 "$logs/$job.log" | sed 's/^/    /'; fi
done
# Timing checks can miss their deadlines while the other jobs load the machine: a job that failed among them runs again
# alone, its first log kept as <job>.parallel.log.
for i in ${again[@]+"${again[@]}"}; do
    job=${names[$i]}
    mv "$logs/$job.log" "$logs/$job.parallel.log"
    run_job "$job" "${runs[$i]}"
    read -r result took <"$logs/$job.result" || { result=FAIL; took=0; }
    line=$(printf '%-11s %s alone (%ss; failed among the others: %s)' "$job" "$result" "$took" "$job.parallel.log")
    echo "$line"
    summary+="$line"$'\n'
    [ "$result" = PASS ] || { failed=1; tail -20 "$logs/$job.log" | sed 's/^/    /'; }
done
printf '%s' "$summary" >"$logs/summary.txt"
[ $failed -eq 0 ] && echo "all jobs passed" || echo "failed: see $logs/<job>.log"
exit $failed
