"""Fails on a former project name (Skylauncher, SSARecomp, SkyBuild, SSABuild) outside tools/ci/naming_allow.txt.

The names kept on purpose, and why, are in the forge's docs/naming.md; the allow list says where they may appear.
Each of its lines is `<path glob> <regex>`: a tracked file matching the glob may hold a line the regex finds (`.` for
any line). A file's own path is checked as its line 0. The same script is in the forge's tools/ci/.

    python3 -I tools/ci/check_names.py [--root DIR]
"""
import argparse
import fnmatch
import re
import subprocess
import sys
from pathlib import Path

OLD = re.compile(r"skylauncher|ssarecomp|skybuild|ssabuild", re.IGNORECASE)
ALLOW = "tools/ci/naming_allow.txt"


def allow_list(root):
    entries = []
    for number, line in enumerate((root / ALLOW).read_text().splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        glob, _, pattern = line.partition(" ")
        if not pattern.strip():
            sys.exit(f"{ALLOW}:{number}: needs a path glob and a regex")
        entries.append((glob, re.compile(pattern.strip())))
    return entries


def hits(root):
    files = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True, check=True).stdout
    for path in filter(None, files.decode().split("\0")):
        if path == ALLOW:
            continue
        if OLD.search(path):
            yield path, 0, path
        file = root / path
        if not file.is_file() or file.is_symlink():
            continue  # a submodule or a link
        data = file.read_bytes()
        if b"\0" in data[:8192]:
            continue  # binary
        for number, line in enumerate(data.decode("utf-8", "replace").splitlines(), 1):
            if OLD.search(line):
                yield path, number, line


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    root = parser.parse_args().root
    entries = allow_list(root)
    used = set()
    found = 0
    for path, number, line in hits(root):
        allowed = [i for i, (glob, pattern) in enumerate(entries)
                   if fnmatch.fnmatch(path, glob) and pattern.search(line)]
        used.update(allowed)
        if not allowed:
            found += 1
            print(f"{path}:{number}: {line.strip()[:160]}")
    for i, (glob, pattern) in enumerate(entries):
        if i not in used:
            found += 1
            print(f"{ALLOW}: `{glob} {pattern.pattern}` allows nothing any more: remove it")
    if found:
        print(f"error: {found} former name(s): use the current one (the forge's docs/naming.md), or add a line to "
              f"{ALLOW}", file=sys.stderr)
        return 1
    print("names: no former name outside the allow list")
    return 0


if __name__ == "__main__":
    sys.exit(main())
