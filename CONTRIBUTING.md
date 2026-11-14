# Contributing to Arkeyan Launcher

Thanks for wanting to help! This page says what is welcome, how to send it, and what a pull request needs before it can
be merged. Questions: the [Discord](https://discord.gg/UMQY4SAS3F) or an [issue](https://github.com/mflr0/Arkeyan-Launcher/issues).

## What you can contribute
- **The launcher**: code, bug fixes, tests, docs, and **translations** (`resources/lang/`).
- **The game ports** (the runtime, the builder, each game's port, the code and shader translators) are public in the
  [arkeyan-forge](https://github.com/arkeyan-forge) organization, under the same rules. Problems in a game are reported
  here, as [issues](https://github.com/mflr0/Arkeyan-Launcher/issues/new/choose), for every repository.
- **Never**: anything from a game (disc images, game files, extracted pictures, sounds, text or code) and code copied or
  adapted from another project. Libraries are added only after an issue and the owner's agreement.

## Before you start
- **A feature or a big change: open an issue first** (or comment on the existing one) and wait for an agreement. Plans
  live in the [milestones](https://github.com/mflr0/Arkeyan-Launcher/milestones); this avoids two people doing the same
  thing, or work that can't be merged.
- **A small fix** (a typo, a translation, an obvious bug): send the pull request directly.

## Branches
- `main` is the last release. **Never target it.**
- `release-X.Y.Z` is the next release (today `release-0.9.6`). **Target it** with your pull request.
- Work in your fork, on a branch of your own (for example `29-xbox-portal-windows`).

## Building and testing
See [Build from source](README.md#build-from-source). Before you send:
```sh
ninja -C build ArkeyanLauncher ArkeyanLauncherTest
./build/ArkeyanLauncherTest --no-disc <an empty scratch folder>
tools/check_format.sh          # tools/check_format.sh --fix formats for you (clang-format 23: pip install clang-format==23.1.4)
```
To hunt memory bugs, build the tests with the sanitizers (CI runs them too):
`cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSKY_SANITIZE=ON`. Every build also checks the
standard library's bounds (`std::vector` indexes...): a wrong index stops the launcher instead of reading other memory.

New behaviour needs a test in `tests/`. A bug fix should come with a test that fails without it when that's possible.
Try your build in **portable mode**, never on your real data: an empty `portable.txt` next to the launcher (on macOS
next to the `.app`) keeps its settings, games, saves and figures in a `data` folder beside it
([docs/install.md](docs/install.md)). `SKY_LAUNCHER_NO_UPDATE_CHECK=1` stops it from looking for updates.

## Code
- C++20, the style in `.clang-format` (4 spaces, braces on their own line, 120 columns). CI checks it.
- Match the code around yours: its naming, its comment density, its helpers. Don't reformat or rename what your change
  doesn't need.
- Text on screen goes through `Tr("...")`, in **English**. Add it to your own language's file in `resources/lang/` if
  you like; the other languages are filled before the release (pull requests only warn about missing ones).
- UI: short labels and the existing components and theme (`src/launcher/ui/components/`, `docs/launcher_ui.md`).
- Update the docs page that covers what you changed (`docs/`).

## Commits and pull requests
- **One subject per pull request.** Its **title** becomes the merged commit's message, so write it as
  `area: what (#n)`: lower-case area, English, 72 characters at most, the issue number when there is one. For example
  `portal: find the Xbox 360 portal on Windows (#29)`. CI checks it.
- **Sign off every commit** (`git commit -s`): it adds `Signed-off-by: Your Name <you@example.com>`, your statement that
  you wrote the change and can give it under the project's license (the
  [Developer Certificate of Origin](https://developercertificate.org/)). CI checks it.
- Fill in the pull request template: the issue, what you changed, how you tested it, on which system.
- Pull requests are **squashed** when merged: one commit per pull request.

## Using AI tools
You may use them, but you are the author: you must understand every line you send, have built and tested it, and be
able to explain it in review. Code you can't explain is not merged. Don't add generated notes, tool names or
co-author lines to commits or files.

## Review and merge
Every pull request is reviewed, and the owner merges it once CI is green (Windows, macOS and Linux builds, the tests,
the format, the title and sign-offs). Expect questions and change requests: they are about the code, not about you.

## License and credits
The project is under the [MIT license](LICENSE). By sending a pull request with signed-off commits, you agree that your
contribution is published under it. Every merged contribution is credited by GitHub name in the release notes of the
version that ships it, and code contributors are thanked in the README.

## Conduct and security
Be respectful: see the [Code of Conduct](CODE_OF_CONDUCT.md). A security problem is never reported in a public issue:
see [SECURITY.md](SECURITY.md).
