# Player profiles

Who plays. Each profile has a pseudo (1 to 15 characters, what a game's name box holds), a picture, a colour (one of the
ten elements') and a stable id. It owns its Toy Box, its saves, its keys and pad, its game languages and options, and its
achievements and play time. The installed games, the discs, the pictures read from them, the video and audio settings and
the launcher's own settings belong to the computer and are shared by every profile.

## In the window
- The sidebar shows the active profile under the title: its picture, its pseudo, "Change profile". Its menu lists the
  profiles (the active one checked), then Add a profile, Edit my profile and Manage the profiles. While a game runs, the
  profiles are greyed: one quits the game to change.
- Changing profile reads its Toy Box, seats, saves, keys and achievements again, and shows its last game.
- Launcher settings > Profiles: every profile with its last game, Edit and Remove (held confirmation; the folder goes to
  `<data>/profiles/.trash/`). The first profile cannot be removed.
- The editor: the pseudo (letters with French accents, digits, space, `- _ . '`; anything else is dropped), Import an image
  (PNG or JPEG, cut to its middle square, kept at 256 px) or Initials, and the colour.

## On disk
```
<data>/profiles/profiles.ini      [profiles] order, active; one [profile.<id>] each: name, color, xuid, picture, last_game
<data>/profiles/<id>/picture.png
<data>/profiles/<id>/profile.ini  the profile's keys, pad, languages and options (not the first profile's)
<data>/profiles/<id>/game.ini     the settings its game reads, handed back when the game ends
<data>/profiles/<id>/Collection/  its Toy Box
<data>/profiles/<id>/<Game>/      its saves, achievements, play time and portal seats (datadir::PlayerGame)
```
The first profile is the data that was there before profiles: nothing moves. Its keys stay in `launcher.ini`, its Toy Box
in `<data>/Collection/`, its saves in `<data>/<Game>/`, and it keeps the Xbox id every game already saw
(`E000000000000001`), so its saves still open. Every other profile gets an offline id of its own (`E00000…`).

## What a game is given
The launcher's environment for the active profile (`profiles::Environment`): `SKY_PLAYER_NAME`, `SKY_PLAYER_PICTURE`,
`SKY_PLAYER_XUID`; for a profile other than the first also `SKY_PROFILE_DIR` (its folder, read by `datadir::PlayerGame`)
and `SKY_COLLECTION`, and `SKY_SETTINGS` pointing at its `game.ini`. A key of the settings file belongs to the player when
`profiles::PersonalKey` says so: `[<game>.input]`, `[<game>.pad]`, and `[<game>]` `language`, `voice_language`,
`nightmare_from_start`.

## Tests
`ArkeyanLauncherTest` runs `ProfilesChecks` (`tests/profiles_test.cpp`; alone with `SKY_LAUNCHER_TEST_PROFILES_ONLY=1`).
`SKY_LAUNCHER_TEST_PROFILES=1` with `SKY_LAUNCHER_SHOTS` and scratch data folders shoots the sidebar, the profile menu,
the new profile editor, a switch and Settings > Profiles (`profiles-01-sidebar` to `profiles-05-settings`).
