# Support: logs, the crash dialog, the portal test

## Where the logs are

In the "Skylanders" data folder (`%APPDATA%\Skylanders\` on Windows, `~/Library/Application Support/Skylanders/` on macOS,
`~/.local/share/Skylanders/` on Linux; docs/install.md):

| File | Written by | What |
| --- | --- | --- |
| `SSA/Arkeyan.log` (`.prev.log`: the run before; `SSARecomp.log` from a game built by a kit up to 0.7.2, read when it is the newer) | the game runtime | its own log: version, OS, CPU, GPU, settings, `[perf]` |
| `logs/skylauncher-game.log` (`.prev.log`) | the launcher | the runtime's stdout and stderr, from its first byte: early crashes, a missing DLL's message |
| `SSA/install.log` (`.prev.log`), `SSA/install-logs/` | the launcher, Arkeyan-Builder | what Arkeyan-Builder printed during the install, and its own step logs kept after the build (docs/install.md) |
| `logs/migration.log` | the first start after an update | what was moved from the older folders |

Settings (the gear) > Support: **Report a problem** (one text file with the last logs, the crash location, the settings and the versions;
see below), **Copy game log** and **Open logs folder** (`logs/`). `SKY_LAUNCHER_LOG_DIR` replaces both log folders (tests).

**The last run** (0.9.5, `LastGameLog`, `src/launcher/game_run.cpp`): `Arkeyan.log` and `skylauncher-game.log` hold the
same lines, so a copy carries one of them: the newest of `logs/skylauncher-game.log` and every game's `Arkeyan.log`; the
capture wins when both were written within 10 s (the same run, with its exit code). The capture names its game on its
second line (`# game giants`, written when PLAY starts it). Copy game log puts the run under one heading,
`===== Game log: Giants, last written 2026-11-10 09:12:03 (local time), skylauncher-game.log =====`; the Game log row
says "Last run: Giants, Today 09:12" (looked up again every second). The game's own log starts with the run's start
(`[log] <path>, started 2026-11-10 09:12:03 (local time)`). Why: a player pasted two copies of the same run and could not
tell an old run from the one just played (issue #20). Tests: `tests/shell_test.cpp` (the capture, an older game log, the
newest game of several, an older launcher's capture).

## Report a problem

**Report a problem** builds one text file and shows it in a window before anything else (Close, Escape or the pad's B
close it). The window has **Save to Desktop** (the file goes to the Desktop, or to the logs folder when the Desktop cannot be
written, and the folder opens in the file manager), **Save as...** (the extension is `.txt`, the name
`ArkeyanLauncher-report-<yyyymmdd-hhmmss>.txt`) and **Copy**; nothing is uploaded.

The file is the one the game writes from its own F3 menu (Report a problem), with the same sections: the header has the launcher,
kit and game build versions, the OS, the CPU and the GPU line from the last game log (plus the disc image path); then
**last crash** (the last `Fault (` block of `Arkeyan.log`, else of `Arkeyan.prev.log`: the guest thread, the guest `lr` and
`ctr`, the guest function `sub_XXXXXXXX` and the host stack; "none in the logs" when there is none); then the files, each under
a `===== name =====` heading:

- `launcher.ini`;
- `logs/migration.log`;
- the last run's log, once (`Game log: <game>, last written <time>, <file>`, chosen as above), then
  `Arkeyan.prev.log` (or `SSARecomp.prev.log`, from an older kit);
- `SSA/install.log`, `SSA/install.prev.log` and every file in `SSA/install-logs/`.

Missing files are named as empty or not found, never skipped silently. Before it is shown the text is redacted
(`src/launcher/report.cpp`): the account name becomes `<user>`, the computer name `<computer>`, the home folder and
`/Users/<name>`, `C:\Users\<name>`, `C:/Users/<name>` and `/home/<name>` become `~`, and the folder holding the
disc image becomes `<iso folder>`. The replacement is case-insensitive, whole names only, one pass; everything else is
kept byte for byte. Unit tests for the redaction, the crash block and the report are in `tests/shell_test.cpp` (`Report: ...`);
the file is written by `src/launcher/problem_report.cpp` (`SaveProblemReport`).

## The launcher stays while the game runs

PLAY starts the runtime beside the launcher's window, which stays open (`GameSession`, `launcher/game_run.cpp`; one game at a
time: `docs/launcher_ui.md`, "A game running"). The runtime's stdout and stderr go straight into `skylauncher-game.log`
(after a `# <runtime> <disc>` line; the launcher adds `# exit code N` when it sees the game end), so the game runs on, and
keeps its log, if the launcher is closed. The game's settings reach it as variables of its environment (`PrepareGameLaunch`).
On Windows the process is started without a console window and its exit code is read from a handle of our own, since SDL
reports none for such processes. (A screenshot script that plays, `SKY_LAUNCHER_TEST_PLAY`, still closes the window and
`src/main.cpp` runs the game with `RunGame`, its output piped, waiting for it.)

If the runtime exits with a non-zero code a dialog over the window says **The game stopped unexpectedly**, with its exit code and the logs
folder, and three buttons: **Report** (writes the same report file as above to the Desktop, shows it in the file manager and opens a new
GitHub issue with a template to attach it to), **Copy log**, **Close**. Nothing is sent by the launcher.

## Portal status

Under COLLECTION's title the launcher shows "Portal: <model>" (for example "Portal: Portal of Power") or "Portal: not
detected"; on the game page, beside the controller, a Pill says "Portal" (a green check) or "No portal" (a dashed circle), with
that line in its tooltip, and opens Launcher settings > Controls. The tooltips say that without a real portal the game uses the
virtual portal (its Toy Box). A worker thread (`src/launcher/portal_status.{h,cpp}`) reads two lists, opening nothing (a
game using the portal is never disturbed), so the UI never waits:
- SDL's HID API (`SDL_hid_enumerate(0x1430, 0)`, Activision's vendor id): once at start, then when
  `SDL_hid_device_change_count()` changes, at most every 2 s;
- the system's USB devices, every 2 s (an Xbox portal is not a HID device, and no change count covers it): Linux reads
  `/sys/bus/usb/devices/*/idVendor` and `idProduct`, macOS the IOKit registry (`IOUSBHostDevice`), Windows the
  configuration manager (`CM_Get_Device_ID_List`, the present `USB\VID_xxxx&PID_yyyy` ids; links `cfgmgr32`).

A device on both lists counts once. Models: `1430:0150` Portal of Power, `1430:1F17` Xbox 360 portal, `1430:09AA` Xbox One
portal (Trap Team's; Windows names it "Xbox Gaming Device"), `24C6:FA00` Xbox 360 portal (from Xenia's table; never seen here), any other Activision id "Skylanders portal (1430:XXXX)". If neither list can be read, nothing is shown. (Before
0.9.5 only the HID list was read: an Xbox 360 portal the game used showed as "Virtual portal".) Checks: `PortalChecks` in
`tests/launcher_test.cpp`: fake enumerators, a fake sysfs folder, Windows ids.

## Test the portal

Settings > Controls > Portal of Power. The button runs `<runtime> --portal-probe` (the game, so it needs to be installed)
and reads its stdout, at most 15 s:

```
{"kind":"hid","vid":"1430","pid":"0150","product":"...","opened":true,"status":"ok"}
{"done":true,"count":1}
```

Transport lines (`{"transport":"usb","available":false,"status":"error: ..."}`) become a note under the result. One JSON
object per line (`kind` `hid` or `usb`, `opened`, `status` `ok` or `error: <why>`; `name` is read as `product` too), then
the `done` line; anything else on stdout is ignored. A HID portal's line also carries `write` (the method that got an
answer) and `methods`, an object of strings (each write method tried and what the portal answered to `R` and `S`); the
parser reads objects of strings one level deep (before 0.9.5 the object ended the line's reading, and `id`, `model` and
`verdict` after it were lost).

The card lists each portal with a pill that never contradicts the verdict under it (0.9.5): **Found** (it answered the
game's reset), **Found, not answering** (it opened and did not answer) or **Found, can't be opened**, then the reason in
the launcher's words (`PortalStatusText`: "The portal didn't answer the game within 1 second.", "The portal refused the
game's command: <the system's reason>.", "Linux needs the portal's udev rule: see the README."...), translated, with the
system's own reason left as it is. Then what the game will do given the mode (`PortalVerdict`): "Real portal: the game will
use it.", "The portal doesn't answer: ..." (opened, no answer), "The portal can't be opened: ..." (none opened), "No portal
found: ...", each ending with "the game will wait" (mode real) or "using the virtual portal". **Copy** beside it puts the
whole result on the clipboard (`PortalProbeText`): the launcher's version and the mode, each portal's line, its model,
every method's line and the method the game writes with, the notes, the verdict. A USB portal's `details` (the runtime's `; `-separated steps) are split: the interfaces and driver on the `device:` line, then each step and packet on its own line (0.9.6: an Xbox 360 portal's start request and first packets, an Xbox One portal's Windows route). Windows' Xbox portals (0.9.6): "This Xbox 360 portal needs Windows' WinUSB driver, set once: see the README." (the README's "Xbox 360 portal on Windows"), "Windows didn't hand the Xbox One portal to the game.". Each portal line of a newer runtime also
carries `id`, `model`, `verdict` and `verdict_reason` (the portal's answer to the game's reset command: "Model: Spyro's
Adventure portal (Xbox 360) (id 01 28)", then "Spyro's Adventure accepts this portal." or, in red, why the game would
reject or warn about it; the contract is the runtime's `docs/portal_probe.md`). A runtime without `--portal-probe` exits
with code 2: "This version of the game doesn't have the portal test yet". Parser, words and tests: `ParsePortalProbe`,
`PortalStatusText`, `PortalVerdict`, `PortalProbeText`, `tests/shell_test.cpp`.

Portal mode is `[portal] mode` = `auto`, `emulated` (Virtual) or `real`; the launcher passes it as `SKY_PORTAL`. Older
`hid` and `usb` values still work and show as "Real portal".
