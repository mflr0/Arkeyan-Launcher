# Portable copy

A game the launcher installed can be exported as one file that plays on its own: no disc image and no launcher are needed
to start it. The game's settings page, **Game** tab, **Portable copy** section, has one button for the system the launcher
runs on: **Export for macOS**, **Export for Windows** or **Export for Linux**.
The button is disabled, with "Install the game first." or "Choose the disc image first." under the section's title, until the game
is built and its disc image is verified; the explanation dialog is closed whenever that is not so (it once opened with "Up to 0 bytes").

Code: `src/launcher/portable_export.{h,cpp}` (the export, no UI), `src/launcher/ui/game_settings_page.cpp`
(`DrawPortableExport`: the section, the message, the folder dialog, the progress), the pack format
`src/shared/collection/portable_pack.{h,cpp}` (shared with the game, which reads it; ArkeyanRuntime's
`docs/getting-started/portable.md` describes the game's side). Test: `PortableChecks` in `ArkeyanLauncherTest --no-disc`.

## What the player sees

1. The button is enabled once the game is installed and its disc image (or its copied game data) is verified.
2. A dialog says what the file is: it holds the player's game, for personal use only, not to be shared; it plays without
   the launcher or the disc image, with the same saves and settings as the installed game; it never updates (export again
   to update it); and its size (up to the disc's, plus the HD textures when they go in). Under it, the settings the copy
   carries (`Settings: 30 FPS · Resolution scale x2 · English`: changed on the Video and Language tabs) and, only when the
   game's HD textures are made, an **Include HD textures** switch, off by default, with both sizes
   (`2.1 GB with them, 1.4 GB without.`). Every language file has the strings.
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
`settings.ini` (`PortableSettings`: `[<game>.video]` as PLAY makes `[video]`, every key written, the game's own frame rate
included; `[<game>.audio]`; `[<game>]` with the languages PLAY would pass, `hd_textures` (`1` only when the copy holds
them) and `nightmare_from_start`; the shared `[input]`), the collection's figures and `collection.json` (not the locks,
not the backups), the game's icon and, with the switch on, every `.dds` of the game's `textures-hd` folder as
`textures-hd/<name>` (where its build reads them: the Games folder when set).

## How it is written

`ExportPortable` checks the room first (the game, the disc's files, the figures, the HD textures when included, plus 64 MB); nothing is written when it is
short. It writes into a hidden folder beside the target (`.<name>.export`), reads the pack's index back, signs the bundle on
macOS, then moves the result into place, replacing an older copy of the same name. A failure or **Stop** leaves nothing behind.
A file name drops the characters Windows can't hold (`Skylanders: Giants` becomes `Skylanders Giants`).

## At its first start

Saves and settings are the player's own folder's (the same ones the installed game uses). The figures in the file are added
to the shared collection when no file of that name exists there (an existing figure is never overwritten), and the keys are
added to `launcher.ini` when the game has no keys there yet, and each section of `settings.ini` when that computer has none
(or an empty one) for it: a computer's own settings win, so one that says `hd_textures=0` plays the copy without them; this happens once per exported copy (again at the next start
if a figure could not be written). The added figures' labels and swapper links join the machine's `collection.json`. The
export refuses a folder inside the installed game's own (the copy would replace it), and an old copy is moved aside until
the new one is in place. The running-game marker
stops the installed game and the portable copy from running at the same time; PLAY says "<game> is running" while the
portable copy plays.

## Checks

- `ArkeyanLauncherTest --no-disc <scratch>` (or `SKY_LAUNCHER_TEST_PORTABLE_ONLY=1`): a stand-in installed game, a folder of
  disc files and a collection with a lock; the pack holds every file, the keys, the settings (the game's own frame rate,
  not the last game's) and the figures but not the lock, and the HD textures only when asked (counted in the room); the
  executable's own bytes are kept; not enough space, a cancel and a missing disc leave nothing; exporting again replaces the copy.
- `ArkeyanLauncherTest --export-portable <disc> <installed game's executable> <folder> [<collection>] [<launcher.ini>]`: the
  same export of a real installed game, for a manual check. `SKY_LAUNCHER_TEST_PORTABLE_DIR=<folder>` makes the dialog's
  button export there without asking.
