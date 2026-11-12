# Level names from the disc

`art::Extract` reads the six games' level display names alongside the upgrade and creation-part text caches.
The `levels` part of `resources/games/<id>/art.json` describes the source; `art::LevelNames` selects this work alone,
and `art::All` includes it. Nothing is downloaded or bundled.

The result is `<data>/<Game>/levels.json`:

```json
{
  "format": 1,
  "reader": 1,
  "game": "example",
  "levels": {
    "invented/room": {
      "en": "Invented room",
      "fr": "Salle inventée"
    }
  }
}
```

The example is invented. Keys are the complete identifiers reported to `game::LevelStarted`, normalized to lower
case with forward slashes. Suffixes, spelling and leading zeroes are preserved. Each value keeps the disc's available
text languages, using the same codes as `upgrades.json`. A consumer selects the game's configured text language,
then English when available, as with upgrades. A missing game, identifier or usable translation leaves presence at
“In a level”. Empty and whitespace-only names have no entry; names are never guessed from filenames.

The file's `reader` field is the extraction version stamp. A current file is retained, including a successful empty
mapping. Missing, malformed, foreign-game or older files are replaced through a temporary file and rename. Deleting
`levels.json` requests another read of the disc. This stamp is independent of the picture cache version.

| Game | Source and link to the runtime identifier |
| --- | --- |
| Spyro's Adventure | `global.str` provides load identifiers. Their case-sensitive CRC keys select names in `text/<language>/global.lxb`; heroic loads use the `Challenge_Title_` keys. |
| Giants | `permanent/global.bld`: `level.bld` pairs the `Level_Loads` and `Level_AdventureTitles` ScriptSets, and `PvP_Level_Loads` and `PvP_LevelTitles`. `StringInfo` order indexes the language packs. `Challenge_Level_BaseString` and `Challenge_Title_` identify heroic challenges. `VillageLoad_Prefix` links hub files to `999_Level_Village(1)`. |
| Swap Force | `archives/zoneinfos.pak`: base `*_zoneinfo.mua` files provide `CZoneInfo::_zoneName`; language-suffixed MUA files link that object to `CZoneInfo::_displayName`. |
| Trap Team | The same script-table method as Giants, with its own `Level_Loads`, `Level_AdventureTitles`, challenge fields and hub prefix, plus paired `Horde_Level_Loads` and `Horde_Level_Titles`. Read whichever language packs the disc contains; English is optional. |
| SuperChargers | `archives/zoneinfos.pak`: base `*_zoneinfo.igz` objects provide `CZoneInfo::_zoneName`; named roots and the language files' external object/field references select `CZoneInfo::_displayName` in `*_zoneinfo_<language>.lng`. |
| Imaginators | The same zone metadata and language-file method as SuperChargers. |

Alchemy's later archives use raw DEFLATE chunks. The shared archive reader decodes these with the existing inflater;
Giants and the earlier archives retain their LZMA path. MUA attributes support XML entities and numeric character
references. Script language packs decode UTF-16, including surrogate pairs, to UTF-8.

`art::ReadLevelNames(game, disc)` returns the mapping in memory without writing a cache.
`art::ReadLevelNamesFrom(manifest, disc)` also accepts an explicit source description, for invented fixtures.
An optional `art::Disc::files` callback lists disc-relative files for matching actual hub stages and excluding stale
script load targets that have no level file.

## Checks

Build with `cmake --build build --target ArkeyanLauncherTest`. The focused mode runs the invented LXB, script, MUA,
IGZ, compression and cache checks without launching an application:

```sh
SKY_LAUNCHER_TEST_LEVEL_NAMES_ONLY=1 ./build/ArkeyanLauncherTest --no-disc <empty-scratch>
```

For disc checks, set any of `SSA_ISO`, `GIANTS_ISO`, `SWAPFORCE_ISO`, `TRAPTEAM_ISO`, `SUPERCHARGERS_ISO` and
`IMAGINATORS_ISO`, and pass the disc image as the first argument:

```sh
SKY_LAUNCHER_TEST_LEVEL_NAMES_ONLY=1 ./build/ArkeyanLauncherTest "$SSA_ISO" <empty-scratch>
```

Each unset variable skips that game's check. Real-disc checks keep text in memory and print counts only. They never
call cache extraction on real disc data. The full existing launcher suite also includes the new fixtures and optional
disc checks, but its other checks start launcher processes and write disc caches; use the focused mode when those
operations are prohibited.
