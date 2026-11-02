# Portable copy

A game the launcher installed can be exported as one file that plays on its own: no disc image and no launcher are needed
to start it. The game's settings page, **Game** tab, **Portable copy** section, has one button for the system the launcher
runs on: **Export for macOS**, **Export for Windows** or **Export for Linux**.

Code: `src/launcher/portable_export.{h,cpp}` (the export, no UI), `src/launcher/ui/game_settings_page.cpp`
(`DrawPortableExport`: the section, the message, the folder dialog, the progress), the pack format
`src/shared/collection/portable_pack.{h,cpp}` (shared with the game, which reads it; ArkeyanRuntime's
`docs/getting-started/portable.md` describes the game's side). Test: `PortableChecks` in `ArkeyanLauncherTest --no-disc`.

## What the player sees

1. The button is enabled once the game is installed and its disc image (or its copied game data) is verified.
2. A dialog says what the file is: it holds the player's game, for personal use only, not to be shared; it plays without
   the launcher or the disc image, with the same saves and settings as the installed game; it never updates (export again
   to update it); and its size (up to the disc's). English and French.
3. **Choose a folder...** opens the system's folder dialog (the Desktop first).
4. The export runs in the background with its progress and a **Stop** button; the section then offers **Show in folder**.
   A failure (not enough space, a disc that can't be read) is a toast and a line under the button.

While an export runs, Reinstall, Clear caches, Delete saves and Uninstall are disabled.

## The file

| System | File | Layout |
|---|---|---|
| macOS | `Skylanders <game>.app` | the installed bundle, plus `Contents/Resources/game.skypack`; the bundle is signed again (ad hoc, `codesign --force --sign -`) |
| Windows | `Skylanders <game>.exe` | the installed executable, then the pack appended to it |
| Linux | `Skylanders <game>.AppImage` | the installed executable, then the pack appended to it (a single executable file, not a squashfs image) |

The pack holds the disc's files as they are (no padding, no compression), `portable.txt` (game, version, kit, the disc's
`default.xex` fingerprint, the export's time), `keys.ini` (the game's `[<game>.input]` section, as PLAY would apply it),
the collection's figures and `collection.json` (not the locks, not the backups), and the game's icon.

## How it is written

`ExportPortable` checks the room first (the game, the disc's files, the figures, plus 64 MB); nothing is written when it is
short. It writes into a hidden folder beside the target (`.<name>.export`), reads the pack's index back, signs the bundle on
macOS, then moves the result into place, replacing an older copy of the same name. A failure or **Stop** leaves nothing behind.
A file name drops the characters Windows can't hold (`Skylanders: Giants` becomes `Skylanders Giants`).

## At its first start

Saves and settings are the player's own folder's (the same ones the installed game uses). The figures in the file are added
to the shared collection when no file of that name exists there (an existing figure is never overwritten), and the keys are
added to `launcher.ini` when the game has no keys there yet; this happens once per exported copy. The running-game marker
stops the installed game and the portable copy from running at the same time; PLAY says "<game> is running" while the
portable copy plays.

## Checks

- `ArkeyanLauncherTest --no-disc <scratch>` (or `SKY_LAUNCHER_TEST_PORTABLE_ONLY=1`): a stand-in installed game, a folder of
  disc files and a collection with a lock; the pack holds every file, the keys and the figures but not the lock; the
  executable's own bytes are kept; not enough space, a cancel and a missing disc leave nothing; exporting again replaces the copy.
- `ArkeyanLauncherTest --export-portable <disc> <installed game's executable> <folder> [<collection>] [<launcher.ini>]`: the
  same export of a real installed game, for a manual check. `SKY_LAUNCHER_TEST_PORTABLE_DIR=<folder>` makes the dialog's
  button export there without asking.
