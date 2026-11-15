#!/bin/sh
# Checks the C++ sources against .clang-format (tools/ci/run.sh runs it; locally: tools/check_format.sh, or --fix to
# apply, or --list to print the files checked). Generated or vendored files are left out.
set -eu
cd "$(dirname "$0")/.."
files=$(git ls-files 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h' | grep -v -E '^src/assets/|embedded_fonts|lucide\.h')
case "${1:-}" in
    --fix) echo "$files" | xargs clang-format -i --style=file ;;
    --list) echo "$files" ;;
    *) echo "$files" | xargs clang-format --dry-run --Werror --style=file ;;
esac
