#!/bin/sh
# Checks a pull request (tools/ci/run.sh; CONTRIBUTING.md): its title is "area: what (#n)" in 72 characters at most (it becomes the
# squashed commit's message).
# Locally: TITLE="portal: ... (#29)" tools/check_pull_request.sh
set -eu
status=0
if ! printf '%s' "$TITLE" | grep -Eq '^[a-z0-9][a-z0-9/_.-]*: [^ ].*$'; then
    echo "error: the title must look like 'area: what (#n)', for example 'portal: find the Xbox 360 portal on Windows (#29)'"
    status=1
fi
if [ "$(printf '%s' "$TITLE" | wc -m)" -gt 72 ]; then
    echo "error: the title is longer than 72 characters"
    status=1
fi
[ $status -eq 0 ] && echo "pull request: title ok"
exit $status
