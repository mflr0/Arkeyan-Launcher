#!/bin/sh
# Checks a pull request (CI; CONTRIBUTING.md): its title is "area: what (#n)" in 72 characters at most (it becomes the
# squashed commit's message), and every commit carries a Signed-off-by line (the DCO).
# Locally: TITLE="portal: ... (#29)" BASE=origin/release-0.9.6 HEAD=HEAD tools/check_pull_request.sh
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
for commit in $(git rev-list --no-merges "$BASE..$HEAD"); do
    if ! git log -1 --format=%B "$commit" | grep -Eq '^Signed-off-by: .+ <.+>$'; then
        echo "error: commit $(git log -1 --format='%h %s' "$commit") has no Signed-off-by line"
        echo "       sign it: git commit --amend -s (or git rebase --signoff $BASE), then push again"
        status=1
    fi
done
[ $status -eq 0 ] && echo "pull request: title and sign-offs ok"
exit $status
