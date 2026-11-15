#!/usr/bin/env bash
# Builds a release on this Mac and makes its draft on GitHub (docs/updates.md, "Releases"):
#
#   tools/release_local.sh vX.Y.Z [--kits <forge or workshop checkout>]
#
# The tag must be pushed and checked out, with a clean tree. Every job of tools/ci/run.sh runs with that version; any
# failure stops before GitHub is touched. Then SHA256SUMS (which the launcher's updater checks), a check that no file
# holds this Mac's home folder, and the draft: a pre-release when the tag is not vX.Y.Z, the notes from
# docs/releases/<tag>.md. --kits then builds the game kits in that checkout and adds them to the draft
# (tools/ci/kits_local.sh there). Publishing the draft stays a separate, manual step.
set -euo pipefail

tag=${1:?usage: tools/release_local.sh vX.Y.Z [--kits <checkout>]}
kits=""
if [ $# -ge 3 ] && [ "$2" = --kits ]; then kits=$(cd "$3" && pwd); fi
[[ $tag =~ ^v[0-9]+\.[0-9]+\.[0-9]+([a-z0-9.-]+)?$ ]] || { echo "error: the tag must be vX.Y.Z or vX.Y.ZbN" >&2; exit 2; }
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
repo=$(gh repo view --json nameWithOwner --jq .nameWithOwner)

[ -z "$(git status --porcelain)" ] || { echo "error: the tree has changes" >&2; exit 1; }
commit=$(git rev-parse "$tag^{commit}")
[ "$commit" = "$(git rev-parse HEAD)" ] || { echo "error: HEAD is not $tag" >&2; exit 1; }
remote=$(git ls-remote origin "refs/tags/$tag^{}" "refs/tags/$tag" | awk 'NR==1 { print $1 }')
[ -n "$remote" ] || { echo "error: $tag is not pushed" >&2; exit 1; }
[ "$(git rev-parse "$remote^{commit}")" = "$commit" ] || { echo "error: origin's $tag is another commit" >&2; exit 1; }
git submodule update -q --init --recursive

dist=$root/.scratch/release/$tag
rm -rf "${dist:?}"
VERSION=$tag tools/ci/run.sh --only format,macos,linux,sanitizers,windows --dist "$dist"

cd "$dist"
assets=(ArkeyanLauncher.exe ArkeyanLauncher-x86_64.AppImage ArkeyanLauncher-macOS.dmg ArkeyanLauncher-macOS.zip)
for asset in "${assets[@]}"; do [ -f "$asset" ] || { echo "error: $asset was not built" >&2; exit 1; }; done
for asset in "${assets[@]}"; do
    if LC_ALL=C grep -aqF "$HOME" "$asset"; then echo "error: $asset holds the home folder's path" >&2; exit 1; fi
done
# The updater checks every download against this file (docs/updates.md): one line per asset.
shasum -a 256 "${assets[@]}" >SHA256SUMS
cat SHA256SUMS

kind=(--draft)
[[ $tag =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || kind+=(--prerelease)
if [ -f "$root/docs/releases/$tag.md" ]; then
    notes=(--notes-file "$root/docs/releases/$tag.md")
else
    notes=(--notes "Built from ${commit:0:7}. You need your own copy of the game; see the README.")
fi
gh release create "$tag" "${assets[@]}" SHA256SUMS "${kind[@]}" --repo "$repo" --verify-tag \
    --title "Arkeyan Launcher $tag" "${notes[@]}"

if [ -n "$kits" ]; then
    KITS_LAUNCHER_REPO=$repo "$kits/tools/ci/kits_local.sh" --attach "$tag"
fi
echo "draft $tag ready: gh release view $tag --repo $repo --web"
