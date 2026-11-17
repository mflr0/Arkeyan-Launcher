#pragma once

// The keyboard keys, pad buttons, rumble and mouse of the games (docs/settings.md, "Keyboard actions"). [input] of
// launcher.ini is the global layout every game inherits (key_<action>=<SDL scancode name>: a key's physical place, as the
// game reads it); [<game>.input] holds a game's own lines, which win. The runtime reads [input], then the game's section.
// Settings > Controls shows the actions every game has (SharedAction: stored in [input]) under "All games"; every other
// action is a game's own, changed on its own tabs and stored in [<game>.input]. A game lists only the actions it uses,
// named as the game names them. No UI: ArkeyanLauncherTest runs these on a settings map.
#include "launcher/launcher.h"
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>
#include <span>
#include <string>
#include <string_view>

struct GameKey
{
    const char* action; // the runtime's name: key_<action> in [input]
    const char* label;  // English, as the game words it (translated where it is shown)
    const char*
        pad;    // the Xbox 360 pad's button the key stands for (shown in brackets for the pad in use: PadButtonName)
    SDL_Scancode fallback; // the game's default key (a physical place: W is Z on an AZERTY keyboard)
    const char* mouse =
        nullptr; // its mouse button once the game's mouse is on ("Mouse Left": attack 1, "Mouse Right": attack 2)
};

// The rows of a game's Keyboard tab, in display order (empty for a game this launcher does not know).
std::span<const GameKey> GameKeys(std::string_view gameId);
// Every action the runtime knows, used or not by a game, and its default key (the runtime's).
std::span<const char* const> InputActions();
const char* InputDefaultKey(std::string_view action);
std::string GameInputSection(std::string_view gameId); // "giants.input"

// The actions every game has (the moves, Back, Pause): their keys are Settings > Controls' "All games" rows.
bool SharedAction(std::string_view action);

// The key's name now: the game's line, else [input]'s, else its default.
std::string GameKeyName(const LauncherConfig& config, std::string_view gameId, const GameKey& key);
// What a game without its own line gets: [input]'s, else the default.
std::string GlobalKeyName(const LauncherConfig& config, const GameKey& key);
// True when the game has its own line (shown in normal text with a reset arrow; else dimmed).
bool GameKeyOwn(const LauncherConfig& config, std::string_view gameId, const GameKey& key);
// The game's own key; the global one removes its line (it inherits again).
void SetGameKey(LauncherConfig& config, std::string_view gameId, const GameKey& key, const std::string& sdlName);
void ClearGameKey(LauncherConfig& config, std::string_view gameId, const GameKey& key);
// The global layout's key, in [input].
void SetGlobalKey(LauncherConfig& config, const GameKey& key, const std::string& sdlName);
// A stored key as the player reads it: the letter its place has on the player's keyboard layout (W's place is "Z" on
// AZERTY), the named keys in the player's language ("Espace"). The stored value stays SDL's English scancode name.
std::string KeyDisplayName(const std::string& sdlName);
// What a captured key stores: its place's SDL name (SDL_GetScancodeName), whatever the layout prints on it.
std::string CapturedKeyName(SDL_Scancode scancode);

// The pad families the button names follow (the game's glyphs do the same: docs/settings.md, "Keyboard actions").
enum class PadFamily
{
    Xbox360,
    XboxOne,
    PlayStation,
    PlayStation3,
    Nintendo
};
PadFamily PadFamilyOf(SDL_GamepadType type);
// The family to name buttons for: a fixed [input] glyphs style wins (xbox360, xboxone, playstation, nintendo; "xbox"
// keeps an Xbox One pad's names, else the 360's), else the pad's (`seen`: the chosen pad, or the last one seen).
PadFamily PadFamilyFor(std::string_view glyphs, PadFamily seen);
// The button a key stands for, named as that family's pad prints it, in the player's language where it is a word
// (PlayStation's shapes, the sticks, the touchpad): "A" on a 360 pad, "Cross" on a PS5, "B" on a Switch (its south).
std::string PadButtonName(const GameKey& key, PadFamily family);
std::string PadButtonName(
    std::string_view xbox360Name, PadFamily family); // the same, from the 360's name ("RB", "D-pad up")
// True when another row of the same game, or a Toy Box key, has the same key.
bool GameKeyConflict(const LauncherConfig& config, std::string_view gameId, const GameKey& key);
// An "All games" row: true when a game that inherits it has the same key on another row, or a Toy Box key has it.
bool GlobalKeyConflict(const LauncherConfig& config, const GameKey& key);
// "Reset to defaults" (a game's Keyboard tab): the game's key_, mouse_ lines and its mouse switch go, so a shared row has
// the global key again and the game's own row its default ([input] holds no key of those after MigrateInput).
void ResetGameKeys(LauncherConfig& config, std::string_view gameId);

// The upgrade to the global layout, once per file ([input] global=1), the same as the runtime's commands::Migrate.
// Older launchers copied a game's keys into [input] at PLAY, so [input] held the last game played. [input]'s keys of the
// shared actions become the global ones; a game's line equal to its global value goes, the others stay as the game's own;
// [input] loses the other actions' keys (a game never seeded keeps them as its own) and its mouse lines, which no game
// read; `seeded` goes. A section an older launcher seeds again is done again. Nobody's keys change. True when it changed.
bool MigrateInput(LauncherConfig& config);

// ---- The Controller tab: what each pad button does, and the rumble (docs/settings.md, "Per-game controller") -----------------
// pad_<button>=<button>|none (absent: the button's own job) and rumble_p1 / rumble_p2=off|low|normal|strong (absent:
// normal), in [input] for every game and in [<game>.input] for one. The runtime reads both; nothing is copied at PLAY.
constexpr int PadButtonTotal = 16;
// The runtime's names of the pad's buttons, in display order: a b x y lb rb lt rt start back ls rs dpad_up .. dpad_right.
std::span<const char* const> PadButtons();
// The button as the pad in use prints it ("RB", "R1", "R", "D-pad up"), words in the player's language.
std::string PadButtonLabel(int button, PadFamily family);
// What the game calls the button's job ("Attack 2"), empty when the game gives it none (the sticks' clicks, the D-pad).
std::string PadActionName(std::string_view gameId, int button);
// The buttons whose job every game has, or none names (Start, Back, the sticks' clicks, the D-pad): Settings > Controls'
// "All games" rows, in [input]; the others are set on each game's Controller tab, in [<game>.input].
bool SharedPadButton(int button);
// The button whose job `button` does now: the game's line, else [input]'s, else its own; -1 for "none" (turned off).
int PadTarget(const LauncherConfig& config, std::string_view gameId, int button);
int GlobalPadTarget(const LauncherConfig& config, int button); // [input]'s, else its own
bool PadOwn(const LauncherConfig& config, std::string_view gameId, int button); // the game has its own line
// Stores the game's line (the global layout's job removes it).
void SetPadTarget(LauncherConfig& config, std::string_view gameId, int button, int target);
void SetGlobalPadTarget(LauncherConfig& config, int button, int target); // [input] (the button's own job: no line)
// "Reset to defaults" (a game's Controller tab): every pad_ and rumble_ line of the game goes.
void ResetPadMap(LauncherConfig& config, std::string_view gameId);
// The rumble choices, in order, and the player's now (player 0 or 1): the game's, else [input]'s, else normal.
std::span<const char* const> RumbleLevels();
std::string RumbleLevel(const LauncherConfig& config, std::string_view gameId, int player);
std::string GlobalRumbleLevel(const LauncherConfig& config, int player);
bool RumbleOwn(const LauncherConfig& config, std::string_view gameId, int player);
// The game's level (the global one removes its line); an empty `gameId`: the global layout's.
void SetRumbleLevel(LauncherConfig& config, std::string_view gameId, int player, const std::string& level);

// ---- The mouse (docs/settings.md, "Mouse") ------------------------------------------------------------------------------
// mouse=0|1 (off by default) and mouse_<action>=<mouse name>|none, in [input] for every game and in [<game>.input] for one
// (the game's line wins), the runtime's names (its host/mouse.h): "Mouse Left", "Mouse Right", "Mouse Middle", "Mouse 4",
// "Mouse 5", "Wheel Up", "Wheel Down". One button per action.
bool GameMouseOn(const LauncherConfig& config, std::string_view gameId);
bool GlobalMouseOn(const LauncherConfig& config);
bool GameMouseOwn(const LauncherConfig& config, std::string_view gameId); // the game has its own switch line
// The game's switch (the global one removes its line); an empty `gameId`: the global layout's.
void SetGameMouseOn(LauncherConfig& config, std::string_view gameId, bool on);
// The button an action is on now, as the runtime reads it: the stored line (the game's, else [input]'s), else the game's
// default (GameKey::mouse) unless another action took that button. Empty for none.
std::string GameMouseName(const LauncherConfig& config, std::string_view gameId, const GameKey& key);
bool GameMouseButtonOwn(const LauncherConfig& config, std::string_view gameId, const GameKey& key);
// Puts `name` (empty: none) on the game's action; the game's action that had it gets none.
void SetGameMouseButton(LauncherConfig& config, std::string_view gameId, const GameKey& key, const std::string& name);
void ClearGameMouseButton(LauncherConfig& config, std::string_view gameId, const GameKey& key);
// A stored mouse name as the player reads it ("Left click", "Wheel up"), translated.
std::string MouseDisplayName(const std::string& name);
// What a click (SDL's button number) or a wheel tick stores; empty for a button the runtime does not know.
std::string CapturedMouseName(int sdlButton);
std::string CapturedWheelName(float y);

// ---- The Toy Box keys: [input] key_toybox, key_toybox_add, key_toybox_clear, shared by the games ----------------------------
struct ToyboxKey
{
    const char* line;     // "key_toybox"
    const char* label;    // English (translated where it is shown)
    const char* fallback; // SDL's key name: F6, F7, F8 (Tab also opens the Toy Box while key_toybox is not set)
};
std::span<const ToyboxKey> ToyboxKeys();
std::string ToyboxKeyName(const LauncherConfig& config, const ToyboxKey& key);
// True when another Toy Box key, or a key of any game, has the same key.
bool ToyboxKeyConflict(const LauncherConfig& config, const ToyboxKey& key);
