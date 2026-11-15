#!/usr/bin/env bash
# Runs the checks on a pull request before it is merged (CONTRIBUTING.md, "Checks"), then posts the result on it.
#
#   tools/ci/check_pr.sh <number> [--macos] [--no-post]
#
# The pull request is checked out in its own worktree under .scratch/pr-<number>, and checked by this checkout's
# tools/ci/run.sh: a pull request cannot change how it is checked. Its code runs only in the containers unless --macos
# is given (read the diff first: that job runs its code on this Mac).
set -euo pipefail

number=${1:?usage: tools/ci/check_pr.sh <number> [--macos] [--no-post]}
shift
macos=0; post=1
for option in "$@"; do
    case $option in
        --macos) macos=1 ;;
        --no-post) post=0 ;;
        *) echo "usage: tools/ci/check_pr.sh <number> [--macos] [--no-post]" >&2; exit 2 ;;
    esac
done
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"

title=$(gh pr view "$number" --json title --jq .title)
base=$(gh pr view "$number" --json baseRefName --jq .baseRefName)
head=$(gh pr view "$number" --json headRefOid --jq .headRefOid)
git fetch -q origin "$base" "pull/$number/head"
tree=$root/.scratch/pr-$number
git worktree remove --force "$tree" 2>/dev/null || rm -rf "${tree:?}"
git worktree add -q --detach "$tree" "$head"
trap 'git -C "$root" worktree remove --force "$tree"' EXIT
git -C "$tree" submodule update -q --init --recursive

jobs=format,pr,linux,sanitizers,windows
[ $macos -eq 1 ] && jobs+=,macos
status=0
# A pull request may add English text without its translations: missing ones are warnings there.
TITLE=$title BASE=origin/$base HEAD=$head SKY_LAUNCHER_TEST_I18N_MISSING_OK=1 \
    "$root/tools/ci/run.sh" --root "$tree" --only "$jobs" || status=$?

summary=$(cat "$tree/.scratch/ci/summary.txt")
[ $macos -eq 1 ] || summary+=$'\n'"macos       not run (the pull request's code runs on the maintainer's Mac only after review)"
if [ $status -eq 0 ]; then verdict="All checks passed"; else verdict="Some checks failed"; fi
body=$(printf '%s on %s.\n\n```\n%s\n```\n' "$verdict" "${head:0:7}" "$summary")
echo "$body"
if [ $post -eq 1 ]; then gh pr comment "$number" --body "$body"; fi
exit $status
