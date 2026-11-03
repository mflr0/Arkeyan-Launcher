# The Collection page on the shared collection

The launcher shows the same figures as the game and the Toy Box: ArkeyanRuntime's collection library (option A of
`figures_overlap.md`, now done). **One model for both apps** (ArkeyanRuntime's `docs/runtime/host/toybox_ux.md`, the
2026-10-26 redesign): the portal's seats on top, the shelf below (every figure once, favorites first), the same cards
and words, a sheet with three main actions. The look is dorydev's: the colours, fonts and widgets are untouched
(`launcher_theme.h`).

## One engine

- `src/shared/collection/` and `src/shared/figures/` (all but `figure_icons.*`) are ArkeyanRuntime's files, copied by
  `tools/sync_collection.sh [--check] <ssa-recomp>` (same file list as ArkeyanRuntime's `tools/sync_skylauncher.sh`, into
  Skylanders' layout). Change them in SSARecomp, where the tests are, and copy them over. The catalogue
  (`resources/collection/catalogue.json`) is built into the executable (`cmake/embed_file.cmake`).
- `figures/figure_icons.*` is the launcher's: `IconStyle` (outline, fills, badge colours) draws the generated icons for any
  catalogue toy and variant kind; the library's own `collection/icons.*` is not used for drawing, only for
  `PortraitPath` (which disc picture a toy gets).
- **Pictures.** Every page uses the newest cached game in reverse catalogue order, regardless of the game being
  viewed. A variant's own picture takes priority over a newer plain picture; otherwise the newest plain picture is
  used, then a family glyph, then the generated icon. `collection::PortraitPath(game, toy, variant, large)` implements
  this with `BestPicturePath` and `HalfPicturePath` through one shared selector. Icon sizes choose the same game and
  variant. A large figure view prefers artwork within that game, while an icon prefers its portrait, with artwork as
  a fallback. Newer plain portraits beat older plain artworks. Known figures pass their exact variant id, including
  in the seats and half chooser; a kind alone is resolved with `VariantFor` when the id is unavailable.
  Existing `<data>/<Game>/portraits/` and `portraits/artworks/` files are used as they are: no cache version change or
  re-extraction. Missing pictures are added by the normal disc check. The shared library's `game_art.md` has the full
  lookup audit and the formats.
- **Variants.** The catalogue lists every variant Spyro's Adventure, Giants and Swap Force know (Series 2, LightCore, Legendary and
  the alternate decos, with the ids Giants reads), and the later games' from public lists; ArkeyanRuntime's
  `docs/runtime/host/collection.md` ("The games' own toy tables") has the sources. Variants of kind `base` are the
  plain figure as sold with a non-zero id (named after the toy).
- **Upgrades.** `collection::Upgrades(toy, game, language)` (`shared/collection/upgrades.h`) gives what each bit of a
  figure's upgrade field means, with the game's own names, descriptions and prices in English or French; the texts
  are read from the player's disc when it is checked (`<data>/<Game>/upgrades.json`, part `art::Upgrades`), else a
  plain list ("Upgrade 1"...). ArkeyanRuntime's `docs/runtime/host/figures.md` ("Upgrades") has the bit layout.
- Removed: `figure_file.*` and `figure_collection.*` (the old engine), replaced by `figure_tag`/`figure_crypto`/
  `figure_data` and `collection`.
- Folder: `collection::Folder()` (`%APPDATA%\Skylanders\Collection`, `~/Library/Application Support/Skylanders/Collection`).
  `SKY_COLLECTION` overrides it (tests, portable installs).

## Swappers (Swap Force): one card, two halves

A Swap Force swapper is two toys, a top and a bottom, each its own dump. The library links them (`Figure::partner`,
`collection::Cards`, `CardName`, `CardLevelText`, `CreateSwapper`, `Pair`, `ChangeHalf`) and the page shows the pair as
**one card** everywhere: the shelf, the seat tile, the sheet, Add a figure and the search.
- **The card**: the swapper's picture, then its name ("Doom Stone", "Magna Jet" for a top and a bottom of two different
  swappers, as the game names them: `collection::CardName`, or the label the player gave), the top's level and favorite
  star, one seat badge. A half on its own (no partner) is a card of its own. A search or a filter that lets one half
  through brings the whole card.
- **The picture** is the game's swap screen: the top's artwork joined to the bottom's at the waist, the
  same on the shelf, the seats, the sheet and Add a figure (and in the game's Toy Box). The game draws each half in place
  on one shared square frame, so each half's picture from the Swap Force disc is brought to the larger frame's size, the
  top is laid over the bottom and the result is cut to its visible pixels (`collection::SwapperPicture`, `JoinHalves`,
  shared with the game, with a unit test on synthetic images); the result is fitted into the card's square. A matched or a mixed swapper, and each version (Dark, Legendary...), has its own halves.
  When a half has no such picture (the pictures are read from the player's disc, so before the first disc check), the card
  shows the top's picture with the bottom small at its corner. Each individual half first uses its own newest half
  picture, then whole-figure art, then its generated icon. A bottom can use its matching top's whole-figure picture
  (Trap Team lists only tops). Whole-figure fallbacks are never joined at the waist; plain halves never replace a dedicated variant picture.
  Neither half is left empty.
- **Putting it on the portal**: a click on a seat, a drag, or the sheet's "Put on the portal" places **both halves**. The seat
  holds the top; `portal.txt` gets two lines for it, the top in the first slot of its seat and the bottom in the next.
  Swap Force's slots are **Player 1: 0 and 1, Player 2: 2 and 3, Magic item: 4, Adventure: 5**; a figure that is not a
  swapper takes the first slot of its seat. Reading the file back, a bottom whose top is on the portal too is not a seat.
- **The sheet** (the top's page) shows both halves ("Top: Doom Stone · Bottom: Doom Stone"), the top's level, **Change the
  bottom** and **Change the top**: a list of the other halves (one row each, those on a seat or in use left out, "Now with
  Blast Zone" or "On its own" under each); the card keeps one half and takes the one chosen, and a swapper that lost it is
  given the old one, so no card disappears. A card on a seat stays there, written again for the new half. A half on its own
  offers the change that finds it a partner. Edit, Reset, Back up and the favorite act on the top.
- **Delete** (hold) sends both halves to the trash and takes the card off its seat; a half on its own deletes alone. Duplicate
  copies both halves, linked. A swapper is in use when either half is held by a running game: the whole card is read-only.
  Export and the other Manage actions work on dumps, so they carry both halves.
- **A figure that was never played** says "New: never played" and shows no "Last played" row.
- **Add a figure** lists a swapper once, by its top ("Doom Stone", 16 of them), and **Create** makes both halves, linked;
  its versions are those of the top, the bottom of the same version when it has one.

## Migration (never loses a figure)

At the first visit to the page (`CollectionPage::Open`):

1. `collection::Migrate(collection::LegacyFolder())` **copies** the old `<SSARecomp prefs>/figures` dumps into the
   collection; originals are never moved, changed or deleted, a second pass copies nothing. The game does the same at
   its start, so either side may go first.
2. The launcher's own earlier state moves once (`[collection] moved=1` in `launcher.ini` records it): favorites
   (`[collection] favourites`, file names) become `collection.json` favorites on the copies; the figures that
   started on the portal (`[portal] place`) become the next session's seats; `[portal] place` is then emptied, because
   `SKY_PORTAL_PLACE` overrides the session file at the game's start. The old keys are left in `launcher.ini` otherwise.
3. The never-played starters the game made on its own (37 of them, one per character) are not the player's: My
   Skylanders hides them (the header counts them), Manage > Tidy moves them to the trash.

## The portal at the next start

- What the game reads at start is `portal.txt` in its data folder (`SKY_SAVE_DIR` in tests, else the game's folder in
  the data root): `<slot> <path>` lines, rewritten by the game at every change. The launcher writes it with seat *i* in
  slot *i* (`WriteSeats`) and reads it back (`ReadSeats`): a figure goes to its slot's seat when it fits, else to the
  first seat that takes it (as the Toy Box does). The seats are per game (`catalogue.json` `games[].seats`). Swap Force's
  portal has six slots (see "Swappers"): `SeatFirstSlot` and `SlotSeat` map the seats to them.
- **Which games**: over the seats, "On the portal for" and a Select ("Spyro's Adventure", as narrow as the longest name, with a
  tooltip) lists the games that can start with prepared seats,
  those the launcher has a runtime for (`SessionFile` not empty): Spyro's Adventure, Giants (Player 1, Player 2, Item or
  adventure) and Swap Force (Player 1, Player 2, Magic item, Adventure). The page opens on the game chosen in the sidebar
  when it is one of them.
- **Placing, two steps**: click a seat (A on it, or LT / RT), it lights up and the bar says "Choose a figure for Player 1 ·
  only what Spyro's Adventure reads"; the shelf then shows only what goes there and the game reads; click a figure: it
  goes on the seat ("Player 1: Splash's"), and the choosing ends. Click the seat again, Cancel, B or Escape stops
  choosing. Dragging a card onto a seat also works; a figure already on the other seat moves (it is on one seat only).
- **Taking off**: the X on the tile (**fixed** in #9: `SetNextItemAllowOverlap()` before the tile's button lets the X win
  where it is; `SKY_LAUNCHER_TEST_SEATX` checks all four seats by mouse, keyboard and pad), Delete or the pad's X on a
  focused tile (or Delete with the mouse over it), the tile's right-click menu ("Take it off", "Choose another", "Open its
  sheet"), or dragging the tile onto the shelf. A seat whose figure is gone shows "Missing figure" and can be emptied.
- **A tile** (a card, its picture with M round it: 72 points high; M between the tiles): the picture, then three lines: the seat's name (blue while choosing for it), the
  figure's name, its level for that game ("Level 10 (15)"); empty: a `+`, "Empty", "Choose". The X is an `IconButton`
  (`Icon::Cross`, tooltip "Take it off") at the corner.
- **Live**: figures a game holds (JSON lock of a live process) read "Playing now"; while the shown game runs a status pill
  "Spyro's Adventure is running: use its Toy Box" follows the game's Select above the seats, the seats are
  read-only and Edit, Reset, Delete are disabled for those figures (favorites and names stay editable). "Mark as not in
  use" (held) breaks a stale lock. A lock only counts as the game running when the game says it runs (its marker,
  `GameIsRunning`): a live process that merely took over the number of a game that crashed holds nothing, the seats stay
  editable, and Manage > Tidy offers "Mark as not in use · N" (held) for those locks.
- Refresh: every 1.5 s the model compares a signature of the folder (names, sizes, times of dumps, locks,
  `collection.json`, the session file) and whether each lock's process still runs; any change reloads the shelf. The
  launcher window closes when the game starts (`main.cpp`), so this is seen when the launcher is opened beside a running
  game or after it.

## The screens

- **Header** (phase B, on the component library; `docs/launcher_ui.md`): the title "Collection" with the real portal's
  state ("Portal: not detected") beside it, `+ Add a figure` (Primary) and **Manage** (a Secondary button with a chevron,
  its menu right-aligned under it: Trash, Backups, Import and export, Unknown toys, Tidy the starters: Manage on that tab;
  Show the collection folder); the buttons have the line to themselves. Under the title, a Caption: "14 figures · 3
  favorites". L below: "On the portal for [Spyro's Adventure]" (the game the seats are for), S below it the seats, then
  the bar.
- **The seats** (above).
- **The bar** (2026-10-27, the Toy Box's too, same words and order; `docs/runtime/host/toybox_ux.md` of the game's repo
  holds the design): the **search field** first and wide ("Name, element, game, level..."), then the **order** (a Select,
  "Sort: Recent": Recent, Name, Level, Element, Game; it is not a filter, so it is no chip and Clear all keeps it), then one
  **Filters** button ("Filters · 2" when two are on) opening a small panel of three Selects, **Element**, **Works in** (the toys a game
  reads: Giants reads every Spyro's Adventure toy, Spyro's Adventure no Giants toy) and **Type** (the types the collection has), and **Clear all**.
  Under the bar, only when something is on, the **tags**: one filter Chip per filter (a check; a click turns it off), **Clear
  all**, and the count at the right ("12 figures", "1 figure"; French "12 figurines"). Nothing matches: an EmptyState "No figure
  matches" with "Clear search and filters"; an empty collection: an EmptyState with "Add a figure" and, under it, "Create the <game> starter pack" (the game chosen: its starter pack's figures).
- **The smart search** (`collection::SearchMatches`, shared with the game): the words all have to match; a name
  (label, nickname, toy, version, file), an element ("fire", "feu"), a game ("giants", "swap force"), a type ("item",
  "objet", "adventure", "mini", "swapper"), a version ("legendary", "lightcore"), "level 10" / "niveau 10"; case and
  accents do not matter, English and French both always work, and one typo is tolerated in a word of 5 letters or more
  ("giatns"). A swapper is found by either half and stays one card.
- **Sort** (the Select of the bar): Recent (the default: favorites first, then what the game took, then by element), Name, Level (highest
  first), Element, Game. The other four ignore favorites. `ShelfFilter::sort`.
- **The shelf** (`CollectionModel::Shelf`, `collection::Group` Smart for the portal's game): every figure once,
  favorites first, then the ones that game took (newest first), then by element; "Unknown toys" follow it.
  Figures the selected game cannot read stay in their usual places for every sort, greyed, with "Not for <game>"
  in the existing state line. Clicking one shows the existing incompatibility toast and leaves the seats unchanged;
  right click still opens its sheet. The saved `fold_foreign` setting has no effect on this shelf.
  The untouched starters the game made on its own
  are left out (Manage > Tidy). A card: the picture, the name (the label, else the toy's name with the game's nickname:
  "Gill Grunt (Splash)"), the level line for the portal's game, the star, the variant's name, P1/P2/"On" for the seats.
  The Toy Box draws the same card.
- **The sheet** (a click on a card when no seat is being chosen, right click, X, F2): the picture with its element and
  type, in a Modal (Large, titled with the figure's name, the cross at the top right; B and Escape close it); the version,
  where it sits (a status pill "On Player 1 at the next start of Spyro's Adventure") or which game holds it (a warning
  pill); **three actions** in a row: **Put on the portal** (Secondary with a chevron; a menu of the seats: "(replaces
  Bash)", a check on its own seat, disabled where it doesn't fit; Take it off), **Edit**, **Favorite** (a star), and a
  "More actions" icon: On the wheel, Duplicate, Back up now, Restore a backup... (opens Manage > Backups on this figure),
  Show in folder, then Reset to new... and Delete... (red menu rows: a danger dialog asks first, the focus on Cancel;
  backed up first; Delete goes to the trash); "Level 10 (15)" with a ProgressBar to the next level, gold and play time,
  last played (`ui::FormatWhen`); **Details** (a Ghost button with a chevron; open by default in a window 800 points high or
  more): nickname, hero points, hat and upgrades, the games it plays in, toy type and variant, file and origin, backups.
  A swapper adds "Change the bottom" / "Change the top"; a figure held by a game, the Danger hold button "Mark as not in use".
- **Edit** (a view of the same Modal, titled "Edit", Cancel and Save bottom right; B goes back to the sheet): one column of
  settings rows (`ui::BeginSettings`: label and hint left, the control right), the name in the lists (empty: the toy's name) and, for a figure that keeps them, the level in
  the portal's game (Spyro's Adventure 1-10 on its own part of the experience, so a figure Giants took to 15 keeps those
  levels; Giants 1-15 on the first two parts of the experience; Swap Force 1-20, the game's own table), experience, Giants' **hat picker** (52 names, a Select), gold, hero points (text fields that take digits), the nickname the game shows.
  Save re-reads the figure and writes only the fields changed (a game may have played it meanwhile); a name alone
  writes no stats (and makes no backup).
- **Add a figure** (a Modal, Large): a game Select (the six games), a SearchField, a **Type** Select (as the bar's), one card per
  character with an "owned x2" Badge; the toys the game reads first, then "Later games" labelled "needs Giants" (a
  SectionHeader each, with its count). A side Card lists the versions as ListRows (the picture in `image`, "owned x2" as `trailing`)
  (the plain toy under its own name, then the variants the chosen game tells apart and the same-tag editions: a variant the game plays
  as the plain toy is not a row, `Catalogue::Read`); the plain toy makes the figure under the id the toy is sold with, and the
  toy's own entry in the catalogue (its `base` kind) is never a second row; the footer holds the optional name, "Put it on Player 1" after a Create, and Close then Create at the right (the dialog stays open after a Create, to make another). A toy with
  versions has a "+3" badge on its card; the panel is titled "Versions", each row with its picture (the toy's own when
  it has none) and its kind (an unknown kind from a newer `catalogue.json` shows as "Other"); the search also matches
  the versions' names ("polar" finds Whirlwind, with Polar Whirlwind chosen).
  The picture of the sheet and of each version row is `collection::BestPicturePath` (the variant's own artwork when the
  game has one; Giants: 99, about 1000x960), fitted into its box by `CollectionPage::DrawPicture`; the shelf's cards
  keep the round portraits. Shot `41-giants-variant-sheet` shows a Series 2 Eruptor.
- **Chevrons** are drawn, never typed: a Button's `trailing` icon (`ui::Icon::ChevronDown` / `ChevronUp`: Details, Put on the
  portal, Manage, Filters) and the Selects' own.
- **Upgrades in Edit** (2026-10-26): `DrawUpgradeEditor`: the toy's upgrades by name (`collection::Upgrades`), the same
  columns, rules and words as the Toy Box (`collection::ToggleUpgrade`...), All / None, the focused one's description;
  the Edit fields scroll above the Save row. Save writes `upgrades` and Giants' Wow Pow (`wowPow`, the extra area's +0x06 bit 0, where Giants writes it when
  Persephone sells it; the sheet works on `collection::EditorBits`, the field with bit 10 for the Wow Pow) only when they changed
  (`MergeEditedStats`). The sheet shows a name once, and "Never" under Last played for a figure no game has written.
- **Texts** (2026-10-26): one short sentence per help line, no path or file name in running text, no help when the
  label is clear, errors as what happened then what to do. The intro of Add a figure, the Edit sheet's backup note,
  the empty-state bodies and the portal selector's tooltip were removed; the rules are in `launcher_ui.md`, "Writing".
- **Manage** (a Modal with `ui::Tabs`; the counts in the tab names): Trash (a settings row per figure: its **name** (read
  from the dump, not the file name), when and why, Put back and Delete for good (Danger, held); 30 days), Backups (a
  ListRow per figure with its count, opening its copies: when, level and gold, Restore (Danger, held)), Import and export
  (files, a folder, drop files or a folder on the window; export everything; show in folder), Unknown toys (name one:
  `catalogue.json` of the collection, applied at the next start), Tidy (the starters, but those on a seat; Danger, held).
  Import adds a dump whose serial a dump already there has (a played copy and an older copy are one toy: only one can sit
  on a portal) and says so in the toast; byte-identical dumps are skipped, files over 1 MB and non-files are refused, and
  a copy is written whole or not at all. Export never overwrites: a file of the same bytes counts as done, another takes
  " (2)". Backups belong to the figure: Rename moves them, Delete takes them to the trash with it (`trash/<id>.backups`),
  Put back and Delete for good follow, a backup of another serial or type is not listed and Restore refuses it; backups
  within one second sort by their number. Restore, Reset, Edit and Import write a new file and rename it over the old.
  Names are kept to 200 bytes without reserved Windows names or a trailing dot; a label is trimmed, without control
  characters, 64 bytes at most; a nickname takes well-formed UTF-8 only, without control characters.
- **Pictures**: `collection::PortraitPath` (the newest cached variant picture, then plain art, then the toy's family
  picture from a disc: SSA's potion, compass and paw icons), else `figures::GenerateIcon` (a shape, the element's colour, the
  variant's badge, initials for characters, and the Toy Box's mark for items, packs and sidekicks:
  `collection::KindGlyph`). Never an empty square.
- **Gamepad** (`docs/launcher_ui.md`, "Navigation model"): the d-pad moves in reading order (the seats, the bar, the
  tags, the shelf), A chooses (a seat: choose for it; a card: put it on the seat being chosen for, else its sheet), B goes back
  (closes the overlay one step, stops choosing, clears the search; then to Home), X opens the sheet (on a seat with a figure:
  takes it off), Y the search, LT / RT choose for the previous/next seat, LB / RB the main page before and after. Cards scrolled out of view aren't drawn; Manage and
  the sheet read the trash and the backups once per change of the collection. While a pad is in use a **PadLegend** closes the
  page (LB RB Pages, X Take it off or Figure sheet, Y Search, LT RT Other seat, A Select, B Back), and a card's tooltip shows under the pointer only.
  Settings pages (the launcher's and each game's) end with a PadLegend (A Select, B Back) under their scrolling column, and
  every dialog (`ui::EndModal`) with one (A Select, B Close).

## Files

`collection_page.{h,cpp}` (page, seats, bar, shelf), `collection_figure.cpp` (the overlay's Modal and its footer, the sheet, Edit),
`collection_add.cpp`, `collection_manage.cpp` (Manage, test seed and screens), `collection_model.{h,cpp}` (no UI: seats,
`ShelfFilter` and `Shelf`, signature polling, settings move, naming unknown toys). `launcher_app.h` only holds a
`CollectionPage` and makes it a friend; `launcher_ui.cpp` calls `Draw`, `Drop`, `ClearIcons` and the test hooks.

## Checks

- `ArkeyanLauncherTest` (`SweepChecks`, issue #9): backups that follow Rename, Delete, Put back and Delete for good, Restore of
  another figure's backup refused, backup order within a second, Restore, Reset and stats replacing the file (a second name
  for the old one keeps the old bytes), Import size limit and a write that dies half-way, same-serial Import, Export that
  never overwrites, nickname and label rules, names (reserved, long, case-only), Tidy keeping the seated starter, and sixteen
  processes taking one lock (exactly one wins). `CollectionChecks` adds the lock of a live process that is not the game.
- `ArkeyanLauncherTest` (`CollectionChecks`): library, migration (originals intact, second pass copies none), stats,
  backups, restore, reset, duplicate, trash, import dedupe, JSON locks (live, stale), seats and `portal.txt`, live
  refresh, settings move, unknown toy naming, icons for every catalogue toy (and the family's mark on items and packs),
  **the shelf** (every figure once, none left out, favorites first; choosing for the Magic item seat shows only the
  item; for a player seat only characters; the Element, Game and search filters, the smart search in both languages with a typo and a level, the sort;
  six games mixed on one shelf, incompatible figures in every sort, per-game Recent dates, both saved `fold_foreign`
  values, changing games, and refusing an incompatible seat without changing `portal.txt`), the card's level ("Level 10"), and
  that Giants and Swap Force can be prepared but not the games without a runtime, **the swapper cards** (a swapper once on the
  shelf whichever half a search or a filter matches, one seat and two lines of `portal.txt` for it, a bottom first in the
  file still one seat, Change the bottom, Delete and Undelete of both halves, a lone half deleted alone). Seat X: 12 of 12. Everything runs in a scratch
  folder (`SKY_COLLECTION`, `SKY_SAVE_DIR`, `SKY_MIGRATE_FROM` set by the test).
- Screenshots: `SKY_LAUNCHER_TEST_COLLECTION=1` with `SKY_LAUNCHER_SHOTS=<dir>` seeds a scratch `SKY_COLLECTION` (it
  refuses without it) and writes `20-collection` to `61-focus-seat`: the shelf, choosing for Player 1 and for the Magic
  item, the Water filter, the sheet, its Details, the delete dialog, Edit, Add a figure (a later game), Manage (trash,
  backups), the live portal (`SKY_LAUNCHER_TEST_FAKE_LOCKS=1`), after a delete, Giants' portal, a level-15 Eruptor and
  its Edit with the hat picker, Add a LightCore, a search with no match, a seat nothing goes on, Swap Force's portal with a
  swapper on Player 1, the swapper's sheet, Change the bottom, choosing for Player 1, and a Giants toy's versions (the plain toy, then the versions Giants tells apart, no
  second row with the toy's own name), then the smart search (an element, a level, a typo: `47-` to `49-`), the Filters panel
  (`50-`), the tags with a count (`51-`) and a sort (`52-`), the menus open (`57-` Manage's, `58-` the sheet's, `59-` Put on the portal) and the
  focus ring on a card and on a seat (`60-`, `61-`), a Trap Team trap's sheet and its Edit (`62-`, `63-`: the villain list and the Evolved switch), then `collection-bottom` with the shelf scrolled to its end and the space under its last row visible. `SKY_LAUNCHER_TEST_COLLECTION=pad` shows the page as a pad player sees it (the legend). With `SKY_LAUNCHER_TEST_SWAPPERS=1` the collection also holds all
  16 swappers matched and four mixed ones, and `53-all-swappers` (Swap Force's swapper cards; use `SKY_LAUNCHER_TEST_SIZE=1700x1500`
  to see them all), `54-mixed-swapper-sheet` and `55-add-swapper-versions` are written. Set `SKYLANDERS_DATA`
  to a folder holding the discs' cached pictures to see them (else the drawn icons). `62-trapteam-greyed` shows figures
  from several games with Trap Team selected, sorted by name, including the later figures greyed on the main shelf.
- End to end with the real game: seats prepared by the launcher, the game started with the same
  `SKY_COLLECTION`/`SKY_SAVE_DIR`, no `SKY_PORTAL_PLACE`: its log says which figures it took and the launcher shows the
  locks while it runs and the stats after.

## Not done

- Player 2 is a seat in the launcher; the game has no second pad yet ("needs a 2nd controller" in its Toy Box).
- Names for figures are typed with the keyboard; no on-screen keyboard (the Toy Box has one for the TV).
- Real-portal copies ("Save a copy") are a Toy Box action; the launcher shows them as `origin: portal` figures.
- The launcher quits when the game starts, so live refresh is for a launcher opened next to a running game.
- The Toy Box's wheel pins (`GetWheel`) and pad glyph hints are not drawn in the launcher.
- The View menu is gone (2026-10-26): `collection::GetView`/`SetView` (`views.<game>.launcher` in `collection.json`) are
  no longer read.

## Deleting a figure (issue #13)

What was wrong:
- The Delete button returned from the figure sheet between `BeginDisabled` and `EndDisabled`. The disabled state leaked
  into the rest of the frame: the page went grey and the launcher stayed unusable.
- A hold button fired again every second while the mouse stayed down, so a refused action repeated its toast.
- `collection::Delete` moved the dump first and ignored the index write. When `collection.json` did not load (the S-03
  guard refuses writes), the file sat in `trash/` with no entry: it was lost to Manage > Trash.

Now: the index is written first and the move undone if it fails, so a refusal changes nothing and comes back as one
toast with its reason. A hold button acts once per press. After a delete the page returns to the shelf, the focus on the
next figure (`CollectionPage::DeleteFigure`, `CollectionModel::Delete`). Tested by `CollectionChecks` and screenshots
`26-figure-delete` (the danger dialog) and `33-after-delete`.

## What a game makes of a figure; the wheel (2026-10-28)
The collection keeps every figure; the library (`src/shared/collection/`, synced) tells what each game reads
(`Catalogue::Read`, `VersionsFor`: the table is in the game's repo, `docs/runtime/host/collection.md` "What each game reads"). The sheet adds a
row "In Giants: plays as Series 2 Eruptor" when the chosen game does not tell the figure's variant apart. The sheet's `...` menu has
**On the wheel: <game>** (a checked row) for each game with a runtime that reads the figure (a toggle, `SetOnWheel`): the figure goes on the wheel of
every player seat of that game (the in-game quick swap wheel, hold Tab or View) and a fact row lists the games. Stored in `collection.json`
(`views.<game>.wheel`, `wheel_skipped`). The screenshot test's `46-` is now `46-add-versions`.

## Picture lookup audit and verification

| Surface | Shared path |
|---|---|
| Collection cards and seats, bottom inset, half chooser, Add a figure | `CollectionPage::Icon` / `DrawIcon` → `PortraitPath`, then `figures::GenerateIcon` |
| Game-page portal panel, figure sheet and version rows | `DrawPicture` → `BestPicturePath`, then `DrawIcon` |
| Swappers on cards, seats, sheets and Add a figure | `DrawSwapper` → `SwapperPicture`; incomplete art → individual top and bottom icons |
| In-game Toy Box, including its wheel | `collection::Portrait` / `Icon` / `PortraitPath`; large views use `BestPicturePath`, pairs use `SwapperPicture` |
| Library icons at any size and disc-image callers | `PortraitPath`, including `DiscImagePath` |
| Individual swapper halves | `HalfPicturePath` and the other picture APIs share the half → whole → generated order |
| Villain vault | `VillainPicturePath` preserves the captured/escaped pictures |
| Traps and items | The ordinary variant → plain → family → generated rule |

The launcher and Toy Box already agreed when using the same API. Portraits previously preferred the viewed game,
large pictures had a separate search and halves searched oldest first. All now use the same selector. The workshop's
`SSACollectionTest` covers separate synthetic game caches, variants, sizes and every incomplete-swapper case;
`ArkeyanLauncherTest --no-disc` checks the synced behavior with synthetic PNGs. No toy family blocks the rule.

A run with the player's discs must confirm that checking them populates these caches, that an older game's page and
the Toy Box show the newest cached portrait of the same figure, and that matched/mixed swappers and their individual
half choosers remain visible when one or both half pictures are absent. It must also check the actual variant art,
trap shapes and captured/escaped villain pictures. These visual checks do not require deleting or re-extracting an
existing art cache.
