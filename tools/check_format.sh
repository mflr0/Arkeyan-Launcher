#!/bin/sh
# Checks the C++ sources against .clang-format (CI runs it; locally: tools/check_format.sh, or --fix to apply).
# Generated or vendored files are left out.
set -eu
cd "$(dirname "$0")/.."
files=$(git ls-files 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h' | grep -v -E '^src/assets/|embedded_fonts|lucide\.h')
if [ "${1:-}" = "--fix" ]; then
    echo "$files" | xargs clang-format -i --style=file
else
    echo "$files" | xargs clang-format --dry-run --Werror --style=file
fi
