# The Toy Box window

The running game's Toy Box in a window of its own, to keep beside the game (on a second monitor, say) and change figures
without leaving the game. It is the Collection page, live: the same seats, cards, search and filters, the same sheet and
Add a figure. A change of a seat goes to the game at once; the game puts the figure on its portal and its own Toy Box (F6)
shows the same seats.

## Opening it
- **While a game runs:** the Play page's **On the portal** card has **Toy Box window**; the Collection page has the same
  button beside "<game> is running". Pressed again while the window is open, it brings the window to the front.
- **With every game:** tick **Open with the game** at the top of the window. From then on the launcher opens it each time it
  starts a game. Kept in `toybox-window.ini` in the launcher's data folder, with the window's place and size.
- It closes by itself when the game ends. It comes back where it was, on the same monitor (a monitor no longer there puts it
  back in the middle of the main one).

## What it shows
- **On the portal of <game>**, with **Live** while the game answers, **Waiting for the game...** while it starts. If the
  game and the launcher are different versions of the link, the window says which to update ("The game is newer: update
  the launcher", "The game is older: update it"); it never changes the seats then.
- The seats: a virtual figure (its card, its level), or a **real figure** when a real portal is plugged in ("Real figure · on
  the portal", read only: lift it off the portal to change it), or nothing.
- Put a figure on a seat as on the Collection page: drop its card on the seat, or click the seat and pick a card, or the
  sheet's **Put on the portal**. A figure on the other player's seat swaps the players. Take it off with the seat's X, its
  menu, Delete, or by dragging it to the shelf. What the game refuses comes back as a message ("The portal is full",
  "Another program is using this figure"...).
- Keyboard, mouse and controller work as on the Collection page; the look is the launcher's. The controller and the keyboard
  go to the window that has the focus (the game's or this one), never to both: click the game to play again.

## How it works
- The window is the launcher started again by itself: `ArkeyanLauncher --toybox <game>`. It holds no lock, starts no game,
  checks no update and leaves the launcher's settings file alone. Code: `src/launcher/ui/toybox_window.cpp` (the window),
  `src/launcher/toybox_link_client.{h,cpp}` (the link), `CollectionModel::live` and `CollectionPage::SetLive` (the page,
  live).
- **The link to the game** (`src/shared/toybox_link.*`, `src/shared/toybox_socket.*`, copied from ArkeyanRuntime by
  `tools/sync_collection.sh`): while it runs, the game listens on the loopback address only (127.0.0.1, a port of the
  system's choosing) and writes the port and a random token in `toybox.link` beside its `portal.txt`, readable by its user
  only. The window connects, says hello with the link's version and the token, and gets the seats; then it sends what the
  player does (`put`, `off`, `swap`, `clear`) and the game answers each, with its seats. Nothing outside the computer can
  connect; one window at a time; a game started without the launcher works as before. The messages: ArkeyanRuntime's
  `docs/runtime/host/toybox_link.md`.
- The window does not decide anything about the portal: the game runs each change as its own Toy Box would (the same
  checks: a figure in use elsewhere, a seat it does not fit, a full portal, a real toy).

## Tests
- `ArkeyanLauncherTest` (`SKY_LAUNCHER_TEST_TOYBOX_ONLY=1 ArkeyanLauncherTest --no-disc <scratch>` for this part alone): the
  link against a fake game on the loopback: hello both ways, the seats, the commands, an answer, another version (newer and
  older), another window, a game that went without a word, a damaged link file.
- `SKY_LAUNCHER_TEST_TOYBOX="<s>:put <seat> <file>|<s>:off <seat>|<s>:choose <seat>|<s>:shot <name>|<s>:quit"` drives the
  window (seconds after the game answered; `put` and `off` are what a drop and a seat's X do) and
  `SKY_LAUNCHER_TOYBOX_SHOTS=<folder>` keeps its pictures. ArkeyanRuntime's `tools/regress/toybox_window.sh` runs it against
  each game: the launcher plays the game with "Open with the game" ticked, the window puts, swaps and takes off figures, and
  the game's log must show the game reading each.
