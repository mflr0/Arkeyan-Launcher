# Arkeyan Launcher: native Skylanders on PC, macOS and Linux

**Play Skylanders natively on Windows PC, macOS and Linux.** A native PC port of the Xbox 360 Skylanders games, by static
recompilation. Arkeyan Launcher builds *Spyro's Adventure*, *Giants*, *Swap Force*,
*Trap Team*, *SuperChargers* and *Imaginators* from your own Xbox 360 disc image into real PC games: no emulator, your own saves, your
figures in a virtual portal, any controller, keyboard or mouse. Formerly Skylauncher: it updates itself to this version.

<!-- SCREENSHOTS: four gameplay pictures (one per game), hosted outside the repository -->

Arkeyan Launcher contains no game code and no game art. Everything shown from the game is read from your own disc and stays
on your computer.

## What you get

- **Native games**, built once on your computer from your disc (2 to 4 minutes on an Apple Silicon Mac), at a higher resolution
  than the console's, at 30 or 60 FPS.
- **No portal needed.** Your figures live in the **Toy Box**: put any of them on the virtual portal from the game (F6), swap
  in one press, create figures from the catalogue, choose the villain in a trap. A real Portal of Power works too.
- **Any controller, the keyboard or the mouse**, every key and button remappable, button pictures that match your device.
- **Your saves are safe**: written in one go, earlier versions kept, restored in two clicks.
- **Updates in one click**, for the launcher and each game, checked against the release's SHA-256.
- **A portable copy** of an installed game in one file, to play it anywhere without the launcher.

## The games

| Game | Mac | Windows | Linux | Played so far |
|---|---|---|---|---|
| Spyro's Adventure | ✅ | ✅ | ⚠️ tested in a container only | 27 story levels and the Ruins played with a hero; bosses and Heroic Challenges not all confirmed |
| Giants | ✅ | ⚠️ tested in Wine only | ⚠️ tested in a container only | Chapter 1, and 78 of its 91 levels in an automatic tour |
| Swap Force | ✅ | ⚠️ tested in Wine only | ⚠️ tested in a container only | The tutorial, the first area of the story and the two-player arenas; 158 of 195 zones in an automatic tour |
| Trap Team | ✅ | ⚠️ not tested yet | ⚠️ not tested yet | The tutorial, the first chapters and the Academy, a villain captured and kept in its trap; 53 of 56 levels in an automatic tour |
| SuperChargers | ✅ | ⚠️ not tested yet | ⚠️ not tested yet | The opening and the Academy, a vehicle on the portal |
| Imaginators | ✅ | ⚠️ not tested yet | ⚠️ not tested yet | The opening and the hub |

Finished a game, or played one on Windows or Linux? Please say so in an
[issue](https://github.com/mflr0/Arkeyan-Launcher/issues): it is how this table gets filled.

**Testers wanted**, above all on a real Windows or Linux PC (and the Steam Deck), and with a real Portal of Power.

## Tested

Every release is checked before it ships, on the four games:

- **A clean install of each game** from the release's own kit, then its first level played by a script in a real window
  (movies on, figures on the virtual portal, frames captured).
- **The full test suite**: each game built from its disc, started and played for a few minutes (frame rate, sound, the
  picture compared with a reference, saves, figures), and thousands of unit checks (figures, saves, the portal, the
  Toy Box, settings, the launcher's install, update and uninstall paths, damaged disc images).
- **Automatic level tours** that load every level of each game one by one.
- **The code translator** is proved against its reference on all 4,294,967,296 possible PowerPC instruction words.
- The Windows and Linux builds of Spyro's Adventure, Giants and Swap Force also run in Wine and in a container.

## What you need

- Your own copy of *Skylanders: Spyro's Adventure*, *Skylanders: Giants*, *Skylanders: Swap Force*, *Skylanders: Trap Team*, *Skylanders: SuperChargers* or *Skylanders: Imaginators* for Xbox 360, as a disc image (`.iso`).
- Windows 10 or 11 (64-bit), a Mac with Apple Silicon, or a Linux PC (below).
- Apple Silicon Mac required (M1 or later).
- An internet connection the first time: INSTALL downloads the game tools (about 30 to 40 MB) and the compiler.
- **Linux:** an x86_64 PC with a Vulkan driver (Mesa or NVIDIA), about 6 GB free, and a distribution from 2022 or later
  (glibc 2.34+: Ubuntu 22.04+, Fedora, Arch; SteamOS on the Steam Deck should work but has not been tested yet). No compiler or root is needed: INSTALL
  downloads zig (55 MB) as the compiler. A real portal needs a udev rule once, the only step that needs `sudo` (on
  the Steam Deck, set a password with `passwd` first):

  ```sh
  echo 'SUBSYSTEM=="hidraw", ATTRS{idVendor}=="1430", ATTRS{idProduct}=="0150", MODE="0660", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/70-skylanders-portal.rules
  sudo udevadm control --reload-rules && sudo udevadm trigger
  ```

  Then unplug the portal and plug it in again.

## Supported discs

Xbox 360 discs only.

| Game | Regions | Languages |
|---|---|---|
| Spyro's Adventure | USA, Europe | English, French, German, Spanish, Italian, Dutch, Swedish, Norwegian, Danish, Finnish |
| Giants | USA, Europe | English, French, German, Spanish, Italian, Dutch, Portuguese, Swedish, Norwegian, Danish, Finnish |
| Swap Force | USA, Australia, Europe | English, French, Spanish, Italian |
| Trap Team | Europe | English, German, French, Dutch |
| SuperChargers | Europe | English, German |
| Imaginators | Europe | French, Dutch |

Another edition is refused with a message. If you have one, tell us in an issue.


## Get started

1. Download Arkeyan Launcher from the [Releases page](https://github.com/mflr0/Arkeyan-Launcher/releases):
   - **Windows:** `ArkeyanLauncher.exe`. If SmartScreen warns you, choose More info, then Run anyway. If Windows Defender
     reports a trojan such as `Bearfoos`, read [Windows Defender alerts](#windows-defender-alerts).
   - **Mac:** `ArkeyanLauncher-macOS.dmg`. The app is not notarized: the first time, macOS blocks it; go to System Settings > Privacy & Security and choose Open Anyway (macOS 14 and earlier: right-click the app and choose Open).
   - **Linux:** `ArkeyanLauncher-x86_64.AppImage`, after `chmod +x`.
2. Drop your `.iso` on the window, or click the button to choose it. The disc is checked.
3. Click **INSTALL**. The game is built from your disc, once: a few minutes (2 to 4 on an Apple Silicon Mac, more on a
   slower computer).
4. Click **PLAY**.

## Controls

Any gamepad works: Xbox, PlayStation, Switch and others. You can also play on the keyboard, and change every key in
Settings > Controls or in the game's F3 menu > Commands.

**Mouse:** turn it on per game (the game's keys page, or F3 > Commands). Left click is attack 1, right click attack 2, and
every mouse button and the wheel can take any action. The game holds the pointer while you play and lets it go in the F3
menu, the Toy Box, on Alt-Tab and when the window loses focus; a click in the window takes it back.

The launcher shows live whether a real Portal of Power is plugged in ("Portal: <model>" or "Portal: not detected"), next
to PLAY and on the Collection page.

| Action | Gamepad | Keyboard |
|---|---|---|
| Move | Left stick | W A S D |
| Camera | Right stick | I J K L |
| A | A | Space |
| B | B | X |
| X | X | C |
| Y | Y | V |
| Pause | Start | Enter |
| Back | Back / View | Backspace (Escape also goes back in menus) |
| LB / RB | LB / RB | Q / E |
| LT / RT | LT / RT | 1 / 3 |
| D-pad | D-pad | Arrow keys |

In Trap Team, LT (1 on the keyboard) switches between your Skylander and the villain in your trap.

On-screen buttons can show Xbox 360, Xbox One / Series, PlayStation, Nintendo or keyboard glyphs (Settings > Controls).

### Figures and the Toy Box

You don't need a real Portal of Power. The game has a virtual portal, and your figures live in the **Toy Box**.

| | Gamepad | Keyboard / mouse |
|---|---|---|
| Open the Toy Box | View + Y | F6, or tap Tab |
| Quick swap wheel | Hold View | Hold Tab |
| Choose / back | A / B | Enter / Escape |
| Options of a figure | X | Right click |
| View menu (sort, group) | Y | F3 |
| Previous / next section | LB / RB | Page Up / Page Down |
| Take a figure off a seat | X, then Take off | Delete |

The **Collection** page of the launcher shows the same figures: their stats, backups, the trash, and the seats for the
next game. You can also create figures from the catalogue there, or a game's whole starter pack. Figures the chosen game
cannot read stay in the list, greyed.

For Trap Team, choose the villain a trap holds and whether it is evolved in the Collection, with the trap off the portal.
No capture needed. The Toy Box shows both.

To use a real portal (Wii, PlayStation, Xbox 360 or Xbox One), plug it in and choose it in Settings > Controls >
Portal of Power. Then click **Test the portal**.

## Settings

- **Resolution scale:** Auto picks x1 on integrated graphics and x2 on dedicated cards. If the game is slow, use x1.
- **Frame rate:** the default is 60 for Giants and Swap Force and 30 (the original) for Spyro's Adventure and Trap Team. 60, 120, 144, 240
  and unlimited are experimental (Giants stops at 60); "Display refresh rate" follows the screen, up to 60.
- **Portable mode:** put an empty file named `portable.txt` next to the launcher (on macOS, next to the app). The
  settings, saves, figures and games then live in a `data` folder beside it, for the launcher and for every game it
  starts. Move the folder and everything comes along.
- The launcher and the games share one settings file, `launcher.ini`.
- **Language:** the launcher is available in English and French (Settings, Launcher tab).
- **Your saves are safe:** each save is written in one go, and the previous version is kept before it is replaced. To go
  back to one, open the game's settings (the gear next to PLAY) > Saves > Restore.

## If something goes wrong

- Settings (the gear) > Support > **Copy game log** copies the log so you can send it to us. **Open logs folder** shows the
  files.
- Everything is in one "Skylanders" folder: `%APPDATA%\Skylanders\` on Windows, `~/Library/Application Support/Skylanders/`
  on Mac, `~/.local/share/Skylanders/` on Linux. The logs are in its `logs/` folder and in each game's folder.
- If the game stops with an error, a window offers Copy log and Report.
- Starting Arkeyan Launcher while it is already open brings its window to the front; a game's shortcut plays that game in it.
- Updates (Arkeyan Launcher's and each game's) are looked for at every start; an offline start says nothing. Every download is checked against the release's SHA-256.

## Windows Defender alerts

Windows Defender may flag `Arkeyan-Builder.exe` (the kit's builder), `ArkeyanLauncher.exe` or a game it builds as
`Trojan:Win32/Bearfoos.A!ml` (or `.B!ml`). The
`!ml` ending means no known malware was found: Defender's machine-learning model guessed, from what the program does. It is
a known false positive for small projects ([one of many reports](https://www.reddit.com/r/computerviruses/comments/1m4cd4n/bearfoos_trojan/)).
Arkeyan Launcher matches that guess on every point:

- **It is not signed.** A code-signing certificate costs hundreds of euros a year; a new unsigned program has no
  reputation with Microsoft yet, and each release starts again from zero.
- **It downloads and runs programs**: the game kit and a compiler, at INSTALL, then the game it builds on your PC.
- **It writes a new program on your PC**: the game is compiled from your disc, so its executable exists nowhere else and
  no antivirus has seen it before.
- **It updates itself** (it replaces its own executable), as a dropper would.

What you can check, and do:

- The source is all here, and every release is built by GitHub Actions from it (the Actions tab shows each build).
- Every download is checked against the release's SHA-256 before it runs.
- Restore the file from Windows Security > Protection history (Allow on device), and add an exclusion for
  `%APPDATA%\Skylanders\kit`. Reporting it to Microsoft as a false positive
  ([file submission](https://www.microsoft.com/wdsi/filesubmission)) helps every other player too.

## Build from source

You need CMake 3.20 or later, Ninja, a C++20 compiler (Clang) and git.

```sh
git clone --recursive https://github.com/mflr0/Arkeyan-Launcher.git
cd Arkeyan-Launcher
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build ArkeyanLauncher
```

The developer docs are in `docs/`: `launcher_ui.md` covers the screens and theme, `install.md` the install,
`collection_ui.md` the figures, `updates.md` the updater and releases, `portable.md` the portable copy, and `support.md` the logs.

## Known issues

- On a real Windows PC only Spyro's Adventure has been played; Giants and Swap Force were run in Wine. On Linux those
  three were run in a container, none on a real PC yet. Trap Team has been played on a Mac only.
- Swap Force: not every mode has been played through yet.
- Trap Team: Spy Rise looks almost black, its voices follow the text language, and LB is not offered as a key yet. A real
  Traptanium portal's speaker has not been tried yet.
- The mouse does nothing in the games' own menus yet: use the keyboard or a controller there.
- The 60 fps option is experimental.
- The Mac app is not notarized, and Windows SmartScreen or Defender may warn about the launcher (see
  [Windows Defender alerts](#windows-defender-alerts)).

## How it works

The ports are made by static recompilation: the game's PowerPC code is translated to C++ and its shaders to Metal,
Vulkan and Direct3D 12 shaders, on your computer, from your disc. A runtime written for these games replaces the Xbox 360
(memory, threads, files, graphics, audio, controllers, the Portal of Power).

Every change is checked by automatic tests: each game is built from its disc, started and played for a few minutes by a
script (frame rate, sound, the picture compared with a reference, saves, figures), and scripted tours load the games'
levels one by one. The Windows and Linux builds of Spyro's Adventure, Giants and Swap Force are also run in Wine and in a
container.

Everything here is made for this project: the launcher, the kits and the ports. The reverse engineering, the architecture
and every design decision are the author's own; AI tools write the code from that design and run the tests.

## Credits and license

Designed by dorydev, made by mflr0, UI/UX by Gumelia.

The ports are built with [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and
[XenosRecomp](https://github.com/hedge-dev/XenosRecomp) (hedge-dev; [squidbus's fork](https://github.com/squidbus/XenosRecomp)),
[plume](https://github.com/renderbag/plume) (renderbag), SDL 3, Dear ImGui and FFmpeg, and adapt parts of
[Xenia](https://github.com/xenia-project/xenia). The figures' format (the
blocks of a tag and their checksums) was learned by reading the Skylanders portal code of
[Dolphin](https://github.com/dolphin-emu/dolphin) and [RPCS3](https://github.com/RPCS3/rpcs3); none of their code is
used. Thank you to all of them.

MIT license, see `LICENSE`. The third-party components are listed in `THIRD_PARTY_NOTICES.md`.

Skylanders, Spyro and Activision are trademarks or registered trademarks of Activision Publishing, Inc. This project is
independent: it is not affiliated with, endorsed or sponsored by Activision Publishing, Inc. You need your own copy of the
game.
