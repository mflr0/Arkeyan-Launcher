#include "launcher/game_keys.h"
#include "launcher/i18n.h"
#include <array>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <optional>
#include <utility>

namespace
{
    constexpr std::array<const char*, 22> Actions = { "a", "b", "x", "y", "start", "back", "lb", "rb", "lt", "rt",
        "left_up", "left_down", "left_left", "left_right", "right_up", "right_down", "right_left", "right_right",
        "dpad_up", "dpad_down", "dpad_left", "dpad_right" };

// The defaults, shared by the games: the pad's buttons sit on the same keys in each (A Space, X C, Y V, B X).
// Names and buttons: each game's own controls screen, read in the running game (docs/settings.md, "Where the names come from").
#define MOVE_ROWS                                                                                                      \
    { "left_up", N_("Move up"), N_("L stick"), SDL_SCANCODE_W },                                                       \
        { "left_down", N_("Move down"), N_("L stick"), SDL_SCANCODE_S },                                               \
        { "left_left", N_("Move left"), N_("L stick"), SDL_SCANCODE_A },                                               \
    {                                                                                                                  \
        "left_right", N_("Move right"), N_("L stick"), SDL_SCANCODE_D                                                  \
    }

    constexpr GameKey Ssa[] = {
        MOVE_ROWS,
        { "a", N_("1st Power / Accept"), "A", SDL_SCANCODE_SPACE, "Mouse Left" },
        { "x", N_("2nd Power"), "X", SDL_SCANCODE_C, "Mouse Right" },
        { "y", N_("Use / Talk"), "Y", SDL_SCANCODE_V },
        { "b", N_("Special Power / Back"), "B", SDL_SCANCODE_X },
        { "back", N_("Skylander Info"), "Back", SDL_SCANCODE_BACKSPACE },
        { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
        { "right_up", N_("Shake / Insert / Swing"), N_("R stick up"), SDL_SCANCODE_I },
        { "right_down", N_("Same"), N_("R stick down"), SDL_SCANCODE_K },
        { "right_left", N_("Same"), N_("R stick left"), SDL_SCANCODE_J },
        { "right_right", N_("Same"), N_("R stick right"), SDL_SCANCODE_L },
    };
    constexpr GameKey Giants[] = {
        MOVE_ROWS,
        { "a", N_("Attack 1 / Accept"), "A", SDL_SCANCODE_SPACE, "Mouse Left" },
        { "x", N_("Attack 2"), "X", SDL_SCANCODE_C, "Mouse Right" },
        { "y", N_("Action"), "Y", SDL_SCANCODE_V },
        { "b", N_("Attack 3 / Back"), "B", SDL_SCANCODE_X },
        { "back", N_("Skylander Info"), "Back", SDL_SCANCODE_BACKSPACE },
        { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
    };
    constexpr GameKey SwapForce[] = {
        MOVE_ROWS,
        { "a", N_("Jump"), "A", SDL_SCANCODE_SPACE },
        { "x", N_("Attack 1"), "X", SDL_SCANCODE_C, "Mouse Left" },
        { "b", N_("Attack 2"), "B", SDL_SCANCODE_X, "Mouse Right" },
        { "y", N_("Attack 3"), "Y", SDL_SCANCODE_V },
        { "rb", N_("Attack 2"), "RB", SDL_SCANCODE_E },
        { "lb", N_("Attack 3"), "LB", SDL_SCANCODE_Q },
        { "lt", N_("Interact"), "LT", SDL_SCANCODE_1 },
        { "rt", N_("Interact"), "RT", SDL_SCANCODE_3 },
        { "back", N_("Skylanders Stats"), "Back", SDL_SCANCODE_BACKSPACE },
        { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
    };
    // LT is the game's tag team. RB fires an attack like B in the tutorial; LB is offered like Y (Attack 3), as in Swap Force
    // (issue #51).
    constexpr GameKey TrapTeam[] = {
        MOVE_ROWS,
        { "a", N_("Jump"), "A", SDL_SCANCODE_SPACE },
        { "x", N_("Attack 1"), "X", SDL_SCANCODE_C, "Mouse Left" },
        { "b", N_("Attack 2"), "B", SDL_SCANCODE_X, "Mouse Right" },
        { "y", N_("Attack 3"), "Y", SDL_SCANCODE_V },
        { "rb", N_("Attack 2"), "RB", SDL_SCANCODE_E },
        { "lb", N_("Attack 3"), "LB", SDL_SCANCODE_Q },
        { "lt", N_("Switch Skylander / Villain"), "LT", SDL_SCANCODE_1 },
        { "rt", N_("Interact"), "RT", SDL_SCANCODE_3 },
        { "back", N_("Skylander/Villain Stats"), "Back", SDL_SCANCODE_BACKSPACE },
        { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
    };

    // SuperChargers' own controls screen (pause menu, read in the running game): the vehicle's words sit on the same buttons as
    // the Skylander's. LB drifts like A, LT switches the co-op driver, RB interacts; the D-pad (Customize) and the stick click
    // (Horn) have no row.
    constexpr GameKey SuperChargers[] = {
        MOVE_ROWS,
        { "a", N_("Jump / Drift (vehicle)"), "A", SDL_SCANCODE_SPACE },
        { "x", N_("Attack 1"), "X", SDL_SCANCODE_C, "Mouse Left" },
        { "y", N_("Attack 2"), "Y", SDL_SCANCODE_V },
        { "b", N_("Brake / Reverse (vehicle)"), "B", SDL_SCANCODE_X, "Mouse Right" },
        { "lb", N_("Drift (vehicle)"), "LB", SDL_SCANCODE_Q },
        { "rb", N_("Interact"), "RB", SDL_SCANCODE_E },
        { "lt", N_("Switch Driver (co-op)"), "LT", SDL_SCANCODE_1 },
        { "rt", N_("Accelerate (vehicle)"), "RT", SDL_SCANCODE_3 },
        { "back", N_("My Team"), "Back", SDL_SCANCODE_BACKSPACE },
        { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
    };
    // Imaginators has no vehicles: SuperChargers' on-foot rows on the same engine's buttons (the rest of its pad is not read
    // yet, so it has no row); its Back is the Skylander profile.
    constexpr GameKey Imaginators[] = {
        MOVE_ROWS,
        { "a", N_("Jump"), "A", SDL_SCANCODE_SPACE },
        { "x", N_("Attack 1"), "X", SDL_SCANCODE_C, "Mouse Left" },
        { "y", N_("Attack 2"), "Y", SDL_SCANCODE_V, "Mouse Right" },
        { "rb", N_("Interact"), "RB", SDL_SCANCODE_E },
        { "back", N_("Skylander Profile"), "Back", SDL_SCANCODE_BACKSPACE },
        { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
    };

    // The SDL names of the keys the launcher words in the player's language (the others, letters and digits, are the same everywhere).
    constexpr std::array<const char*, 16> KeyNames = { N_("Space"), N_("Return"), N_("Backspace"), N_("Escape"),
        N_("Up"), N_("Down"), N_("Left"), N_("Right"), N_("Left Shift"), N_("Right Shift"), N_("Left Ctrl"),
        N_("Right Ctrl"), N_("Left Alt"), N_("Right Alt"), N_("Caps Lock"), N_("Delete") };

    std::string KeyLine(const char* action) { return std::string("key_") + action; }
    std::string MouseLine(const char* action) { return std::string("mouse_") + action; }

    const std::string* Find(const LauncherConfig& config, const std::string& section, const std::string& key)
    {
        auto entry = config.find(section);
        if (entry == config.end())
            return nullptr;
        auto value = entry->second.find(key);
        return value == entry->second.end() || value->second.empty() ? nullptr : &value->second;
    }

    // Names of one key place (SDL's lookup ignores case: a hand-edited "f7" is F7).
    bool SameKey(const std::string& a, const std::string& b)
    {
        return SDL_GetScancodeFromName(a.c_str()) == SDL_GetScancodeFromName(b.c_str());
    }

    // The game's line, else [input]'s.
    const std::string* FindLine(const LauncherConfig& config, std::string_view gameId, const std::string& key)
    {
        if (const std::string* value = Find(config, GameInputSection(gameId), key))
            return value;
        return Find(config, "input", key);
    }

    void Erase(LauncherConfig& config, const std::string& section, const std::string& key)
    {
        if (auto entry = config.find(section); entry != config.end())
            entry->second.erase(key);
    }

    bool On(const std::string& value) { return value == "1" || value == "true" || value == "on" || value == "yes"; }

    constexpr std::array<const char*, 6> Shared = { "left_up", "left_down", "left_left", "left_right", "back",
        "start" };

    // The runtime's default keys (its g_keyActions): each game's rows have the same ones.
    const char* DefaultKey(std::string_view action)
    {
        static constexpr std::pair<const char*, const char*> Defaults[] = { { "a", "Space" }, { "b", "X" },
            { "x", "C" }, { "y", "V" }, { "start", "Return" }, { "back", "Backspace" }, { "lb", "Q" }, { "rb", "E" },
            { "lt", "1" }, { "rt", "3" }, { "left_up", "W" }, { "left_down", "S" }, { "left_left", "A" },
            { "left_right", "D" }, { "right_up", "I" }, { "right_down", "K" }, { "right_left", "J" },
            { "right_right", "L" }, { "dpad_up", "Up" }, { "dpad_down", "Down" }, { "dpad_left", "Left" },
            { "dpad_right", "Right" } };
        for (const auto& [name, key] : Defaults)
            if (action == name)
                return key;
        return "";
    }

    // One game's section: what PLAY made of [input] and of its copy before (seeded: the launcher's copy of [input]; key
    // lines it lacked were the defaults) is now its own lines over the global layout `shared` (the runtime's
    // commands::Migrate does the same).
    void MigrateSection(std::map<std::string, std::string>& section, const std::map<std::string, std::string>& shared)
    {
        const bool seeded = section.count("seeded") != 0;
        auto sharedValue = [&](const std::string& key) -> const std::string* {
            auto value = shared.find(key);
            return value == shared.end() || value->second.empty() ? nullptr : &value->second;
        };
        for (const char* action : Actions)
        {
            const std::string line = KeyLine(action);
            auto own = section.find(line);
            const bool has = own != section.end() && !own->second.empty();
            const std::string* global = sharedValue(line);
            if (!SharedAction(action))
            {
                if (!has && !seeded && global != nullptr)
                    section[line] = *global; // what the first copy would have given it
                continue;
            }
            const std::string globalKey = global != nullptr ? *global : DefaultKey(action);
            if (has && SameKey(own->second, globalKey))
                section.erase(own);
            else if (!has && seeded && !SameKey(DefaultKey(action), globalKey))
                section[line] = DefaultKey(action);
        }
        for (auto line = section.begin(); line != section.end();)
        {
            const std::string& key = line->first;
            const std::string* global = sharedValue(key);
            bool drop = false;
            if (IsPadRemapLine(key))
                drop = global != nullptr ? *global == line->second : line->second == key.substr(4);
            else if (key == "rumble_p1" || key == "rumble_p2")
                drop = line->second == (global != nullptr ? *global : "normal");
            else if (key == "mouse")
                drop = On(line->second) == (global != nullptr && On(*global));
            line = drop ? section.erase(line) : std::next(line);
        }
        section.erase("seeded");
    }
}

std::span<const GameKey> GameKeys(std::string_view gameId)
{
    if (gameId == "ssa")
        return Ssa;
    if (gameId == "giants")
        return Giants;
    if (gameId == "swapforce")
        return SwapForce;
    if (gameId == "trapteam")
        return TrapTeam;
    if (gameId == "superchargers")
        return SuperChargers;
    if (gameId == "imaginators")
        return Imaginators;
    return {};
}

std::span<const char* const> InputActions() { return Actions; }

const char* InputDefaultKey(std::string_view action) { return DefaultKey(action); }

std::string GameInputSection(std::string_view gameId) { return std::string(gameId) + ".input"; }

bool SharedAction(std::string_view action)
{
    for (const char* shared : Shared)
        if (action == shared)
            return true;
    return false;
}

std::string GameKeyName(const LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    if (const std::string* value = FindLine(config, gameId, KeyLine(key.action)))
        return *value;
    return SDL_GetScancodeName(key.fallback);
}

std::string GlobalKeyName(const LauncherConfig& config, const GameKey& key)
{
    if (const std::string* value = Find(config, "input", KeyLine(key.action)))
        return *value;
    return SDL_GetScancodeName(key.fallback);
}

bool GameKeyOwn(const LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    return Find(config, GameInputSection(gameId), KeyLine(key.action)) != nullptr;
}

void SetGameKey(LauncherConfig& config, std::string_view gameId, const GameKey& key, const std::string& sdlName)
{
    if (SameKey(sdlName, GlobalKeyName(config, key)))
        ClearGameKey(config, gameId, key);
    else
        config[GameInputSection(gameId)][KeyLine(key.action)] = sdlName;
}

void ClearGameKey(LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    Erase(config, GameInputSection(gameId), KeyLine(key.action));
}

void SetGlobalKey(LauncherConfig& config, const GameKey& key, const std::string& sdlName)
{
    config["input"][KeyLine(key.action)] = sdlName;
}

std::string KeyDisplayName(const std::string& sdlName)
{
    std::string shown = sdlName;
    if (SDL_Scancode place = SDL_GetScancodeFromName(sdlName.c_str()); place != SDL_SCANCODE_UNKNOWN)
        if (const char* name = SDL_GetKeyName(SDL_GetKeyFromScancode(place, SDL_KMOD_NONE, false));
            name != nullptr && *name != 0)
            shown = name;
    for (const char* name : KeyNames)
        if (shown == name)
            return i18n::Tr(name);
    return shown;
}

std::string CapturedKeyName(SDL_Scancode scancode) { return SDL_GetScancodeName(scancode); }

PadFamily PadFamilyOf(SDL_GamepadType type)
{
    switch (type)
    {
    case SDL_GAMEPAD_TYPE_XBOXONE: return PadFamily::XboxOne;
    case SDL_GAMEPAD_TYPE_PS3: return PadFamily::PlayStation3;
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return PadFamily::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return PadFamily::Nintendo;
    default: return PadFamily::Xbox360;
    }
}

PadFamily PadFamilyFor(std::string_view glyphs, PadFamily seen)
{
    if (glyphs == "xbox360")
        return PadFamily::Xbox360;
    if (glyphs == "xboxone")
        return PadFamily::XboxOne;
    if (glyphs == "playstation")
        return PadFamily::PlayStation;
    if (glyphs == "nintendo")
        return PadFamily::Nintendo;
    if (glyphs == "xbox")
        return seen == PadFamily::XboxOne ? PadFamily::XboxOne : PadFamily::Xbox360;
    return seen;
}

std::string PadButtonName(const GameKey& key, PadFamily family)
{
    return PadButtonName(std::string_view(key.pad), family);
}

std::string PadButtonName(std::string_view pad, PadFamily family)
{
    // Per Xbox 360 name: Xbox One / Series, PlayStation 4 / 5, PlayStation 3, Nintendo Switch (by place: the 360's A is
    // the Switch's B). N_: the words are translated.
    static const std::pair<const char*, std::array<const char*, 4>> Names[] = {
        { "A", { "A", N_("Cross"), N_("Cross"), "B" } },
        { "B", { "B", N_("Circle"), N_("Circle"), "A" } },
        { "X", { "X", N_("Square"), N_("Square"), "Y" } },
        { "Y", { "Y", N_("Triangle"), N_("Triangle"), "X" } },
        { "Back", { "View", N_("Touchpad"), "Select", "-" } },
        { "Start", { "Menu", "Options", "Start", "+" } },
        { "LB", { "LB", "L1", "L1", "L" } },
        { "RB", { "RB", "R1", "R1", "R" } },
        { "LT", { "LT", "L2", "L2", "ZL" } },
        { "RT", { "RT", "R2", "R2", "ZR" } },
        { "LS", { "LS", "L3", "L3", "LS" } },
        { "RS", { "RS", "R3", "R3", "RS" } },
    };
    if (family != PadFamily::Xbox360)
    {
        size_t column = family == PadFamily::XboxOne        ? 0
                        : family == PadFamily::PlayStation  ? 1
                        : family == PadFamily::PlayStation3 ? 2
                                                            : 3;
        for (const auto& [xbox360, names] : Names)
            if (pad == xbox360)
                return i18n::Tr(names[column]);
    }
    // The 360's names as printed on the pad (Back stays Back, not the B button's "Back" action); the sticks' are words.
    return pad.find("stick") != std::string_view::npos || pad.starts_with("D-pad") ? i18n::Tr(std::string(pad).c_str())
                                                                                   : std::string(pad);
}

bool GameKeyConflict(const LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    const std::string name = GameKeyName(config, gameId, key);
    for (const GameKey& other : GameKeys(gameId))
        if (&other != &key && SameKey(GameKeyName(config, gameId, other), name))
            return true;
    for (const ToyboxKey& toybox : ToyboxKeys())
        if (SameKey(ToyboxKeyName(config, toybox), name))
            return true;
    return false;
}

bool GlobalKeyConflict(const LauncherConfig& config, const GameKey& key)
{
    const std::string name = GlobalKeyName(config, key);
    for (const ToyboxKey& toybox : ToyboxKeys())
        if (SameKey(ToyboxKeyName(config, toybox), name))
            return true;
    for (const GameInfo& game : g_games)
        for (const GameKey& row : GameKeys(game.id))
            if (std::string_view(row.action) == key.action && !GameKeyOwn(config, game.id, row))
                for (const GameKey& other : GameKeys(game.id))
                    if (&other != &row && SameKey(GameKeyName(config, game.id, other), name))
                        return true;
    return false;
}

void ResetGameKeys(LauncherConfig& config, std::string_view gameId)
{
    auto section = config.find(GameInputSection(gameId));
    if (section == config.end())
        return;
    for (const char* action : Actions)
    {
        section->second.erase(KeyLine(action));
        section->second.erase(MouseLine(action));
    }
    section->second.erase("mouse");
}

bool MigrateInput(LauncherConfig& config)
{
    const bool first = Find(config, "input", "global") == nullptr;
    bool changed = first;
    if (first)
        for (const GameInfo& game : g_games)
            config[GameInputSection(game.id)]; // a game never seeded yet would have started from [input]
    if (first)
        if (auto shared = config.find("input"); shared != config.end())
            for (auto line = shared->second.begin(); line != shared->second.end();)
                line = line->first == "mouse" || line->first.starts_with("mouse_") ? shared->second.erase(line)
                                                                                   : std::next(line);
    const std::map<std::string, std::string> shared =
        config.count("input") != 0 ? config["input"] : std::map<std::string, std::string>();
    for (auto& [name, section] : config)
        if (name.size() > 6 && name.ends_with(".input") && (first || section.count("seeded") != 0))
        {
            MigrateSection(section, shared);
            changed = true;
        }
    if (!changed)
        return false;
    // The other actions are each game's own now: an older launcher's PLAY may have put them back.
    for (const char* action : Actions)
        if (!SharedAction(action))
            Erase(config, "input", KeyLine(action));
    config["input"]["global"] = "1";
    for (auto section = config.begin(); section != config.end();)
        section =
            section->first.ends_with(".input") && section->second.empty() ? config.erase(section) : std::next(section);
    return true;
}

namespace
{
    // Display order = the runtime's g_padButtons; the 360's names, which the games' rows (GameKey::pad) use.
    constexpr std::array<const char*, PadButtonTotal> PadNames = { "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start",
        "back", "ls", "rs", "dpad_up", "dpad_down", "dpad_left", "dpad_right" };
    constexpr std::array<const char*, PadButtonTotal> PadXbox = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Start",
        "Back", "LS", "RS", N_("D-pad up"), N_("D-pad down"), N_("D-pad left"), N_("D-pad right") };
    constexpr std::array<const char*, 4> Rumble = { "off", "low", "normal", "strong" };

    std::string PadLine(int button) { return std::string("pad_") + PadNames[size_t(button)]; }
}

std::span<const char* const> PadButtons() { return PadNames; }

std::string PadButtonLabel(int button, PadFamily family)
{
    return button >= 0 && button < PadButtonTotal ? PadButtonName(PadXbox[size_t(button)], family) : std::string();
}

std::string PadActionName(std::string_view gameId, int button)
{
    if (button < 0 || button >= PadButtonTotal)
        return {};
    for (const GameKey& key : GameKeys(gameId))
        if (std::string_view(key.pad) == PadXbox[size_t(button)])
            return i18n::Tr(key.label);
    return {};
}

namespace
{
    int PadValue(const std::string* value, int button)
    {
        if (value == nullptr)
            return button;
        for (int i = 0; i < PadButtonTotal; i++)
            if (*value == PadNames[size_t(i)])
                return i;
        return *value == "none" ? -1 : button;
    }

    const char* RumbleLine(int player) { return player == 0 ? "rumble_p1" : "rumble_p2"; }

    std::string Level(const std::string* value)
    {
        for (const char* level : Rumble)
            if (value != nullptr && *value == level)
                return level;
        return "normal";
    }
}

bool SharedPadButton(int button)
{
    const std::string_view name = button >= 0 && button < PadButtonTotal ? PadNames[size_t(button)] : "";
    return name == "start" || name == "back" || name == "ls" || name == "rs" || name.starts_with("dpad_");
}

int PadTarget(const LauncherConfig& config, std::string_view gameId, int button)
{
    return PadValue(FindLine(config, gameId, PadLine(button)), button);
}

int GlobalPadTarget(const LauncherConfig& config, int button)
{
    return PadValue(Find(config, "input", PadLine(button)), button);
}

bool PadOwn(const LauncherConfig& config, std::string_view gameId, int button)
{
    return Find(config, GameInputSection(gameId), PadLine(button)) != nullptr;
}

void SetPadTarget(LauncherConfig& config, std::string_view gameId, int button, int target)
{
    const std::string section = GameInputSection(gameId);
    if (target == GlobalPadTarget(config, button))
    {
        Erase(config, section, PadLine(button));
        return;
    }
    config[section][PadLine(button)] = target < 0 ? "none" : PadNames[size_t(target)];
}

void SetGlobalPadTarget(LauncherConfig& config, int button, int target)
{
    if (target == button)
        Erase(config, "input", PadLine(button));
    else
        config["input"][PadLine(button)] = target < 0 ? "none" : PadNames[size_t(target)];
}

void ResetPadMap(LauncherConfig& config, std::string_view gameId)
{
    auto section = config.find(gameId.empty() ? "input" : GameInputSection(gameId));
    if (section == config.end())
        return;
    if (gameId.empty())
        for (const ToyboxPad& binding : ToyboxPads())
            section->second.erase(binding.line);
    for (int i = 0; i < PadButtonTotal; i++)
        section->second.erase(PadLine(i));
    section->second.erase(RumbleLine(0));
    section->second.erase(RumbleLine(1));
}

std::span<const char* const> RumbleLevels() { return Rumble; }

std::string RumbleLevel(const LauncherConfig& config, std::string_view gameId, int player)
{
    return Level(FindLine(config, gameId, RumbleLine(player)));
}

std::string GlobalRumbleLevel(const LauncherConfig& config, int player)
{
    return Level(Find(config, "input", RumbleLine(player)));
}

bool RumbleOwn(const LauncherConfig& config, std::string_view gameId, int player)
{
    return Find(config, GameInputSection(gameId), RumbleLine(player)) != nullptr;
}

void SetRumbleLevel(LauncherConfig& config, std::string_view gameId, int player, const std::string& level)
{
    if (gameId.empty())
        config["input"][RumbleLine(player)] = level;
    else if (level == GlobalRumbleLevel(config, player))
        Erase(config, GameInputSection(gameId), RumbleLine(player));
    else
        config[GameInputSection(gameId)][RumbleLine(player)] = level;
}

namespace
{
    // The runtime's mouse names (host/mouse.h), and how the player reads them.
    constexpr std::array<std::pair<const char*, const char*>, 7> MouseNames = { { { "Mouse Left", N_("Left click") },
        { "Mouse Right", N_("Right click") }, { "Mouse Middle", N_("Middle click") },
        { "Mouse 4", N_("Back side button") }, { "Mouse 5", N_("Forward side button") }, { "Wheel Up", N_("Wheel up") },
        { "Wheel Down", N_("Wheel down") } } };

    bool IsMouseName(std::string_view name)
    {
        for (const auto& [stored, shown] : MouseNames)
            if (name == stored)
                return true;
        return false;
    }

    // What mouse_<action> says in one section: the button, "" for none; nullopt when there is no line.
    std::optional<std::string> MouseIn(const LauncherConfig& config, const std::string& section, const char* action)
    {
        auto entry = config.find(section);
        if (entry == config.end())
            return std::nullopt;
        auto value = entry->second.find(MouseLine(action));
        if (value == entry->second.end())
            return std::nullopt;
        return IsMouseName(value->second) ? value->second : std::string();
    }

    // The game's line, else [input]'s.
    std::optional<std::string> StoredMouse(const LauncherConfig& config, std::string_view gameId, const char* action)
    {
        if (std::optional<std::string> own = MouseIn(config, GameInputSection(gameId), action))
            return own;
        return MouseIn(config, "input", action);
    }

    // The button an action is on, the switch aside (mouse::Effective's rule).
    std::string MouseButtonOf(const LauncherConfig& config, std::string_view gameId, const GameKey& key)
    {
        if (std::optional<std::string> stored = StoredMouse(config, gameId, key.action))
            return *stored;
        if (key.mouse == nullptr)
            return {};
        // The default goes when an action with a line of its own took the button.
        for (const GameKey& other : GameKeys(gameId))
            if (&other != &key)
                if (std::optional<std::string> stored = StoredMouse(config, gameId, other.action);
                    stored && *stored == key.mouse)
                    return {};
        return key.mouse;
    }

    constexpr ToyboxKey Toybox[] = { { "key_toybox", N_("Open the Toy Box"), "F6" },
        { "key_toybox_add", N_("Add a figure"), "F7" }, { "key_toybox_clear", N_("Clear the figures"), "F8" } };
}

bool GameMouseOn(const LauncherConfig& config, std::string_view gameId)
{
    const std::string* value = FindLine(config, gameId, "mouse");
    return value != nullptr && On(*value);
}

bool GlobalMouseOn(const LauncherConfig& config)
{
    const std::string* value = Find(config, "input", "mouse");
    return value != nullptr && On(*value);
}

bool GameMouseOwn(const LauncherConfig& config, std::string_view gameId)
{
    return Find(config, GameInputSection(gameId), "mouse") != nullptr;
}

void SetGameMouseOn(LauncherConfig& config, std::string_view gameId, bool on)
{
    if (gameId.empty())
        config["input"]["mouse"] = on ? "1" : "0";
    else if (on == GlobalMouseOn(config))
        Erase(config, GameInputSection(gameId), "mouse");
    else
        config[GameInputSection(gameId)]["mouse"] = on ? "1" : "0";
}

std::string GameMouseName(const LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    return GameMouseOn(config, gameId) ? MouseButtonOf(config, gameId, key) : std::string();
}

bool GameMouseButtonOwn(const LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    return MouseIn(config, GameInputSection(gameId), key.action).has_value();
}

void SetGameMouseButton(LauncherConfig& config, std::string_view gameId, const GameKey& key, const std::string& name)
{
    const std::string section = GameInputSection(gameId);
    if (!name.empty())
        for (const GameKey& other : GameKeys(gameId))
            if (&other != &key && MouseButtonOf(config, gameId, other) == name)
                config[section][MouseLine(other.action)] = "none";
    if (MouseIn(config, "input", key.action) == name)
        Erase(config, section, MouseLine(key.action)); // what [input] gives it: inherited again
    else
        config[section][MouseLine(key.action)] = name.empty() ? "none" : name;
}

void ClearGameMouseButton(LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    Erase(config, GameInputSection(gameId), MouseLine(key.action));
}

std::string MouseDisplayName(const std::string& name)
{
    for (const auto& [stored, shown] : MouseNames)
        if (name == stored)
            return i18n::Tr(shown);
    return name;
}

std::string CapturedMouseName(int sdlButton)
{
    switch (sdlButton)
    {
    case SDL_BUTTON_LEFT: return "Mouse Left";
    case SDL_BUTTON_RIGHT: return "Mouse Right";
    case SDL_BUTTON_MIDDLE: return "Mouse Middle";
    case SDL_BUTTON_X1: return "Mouse 4";
    case SDL_BUTTON_X2: return "Mouse 5";
    default: return {};
    }
}

std::string CapturedWheelName(float y) { return y > 0 ? "Wheel Up" : y < 0 ? "Wheel Down" : std::string(); }

std::span<const ToyboxKey> ToyboxKeys() { return Toybox; }

std::string ToyboxKeyName(const LauncherConfig& config, const ToyboxKey& key)
{
    const std::string* value = Find(config, "input", key.line);
    return value != nullptr ? *value : key.fallback;
}

bool ToyboxKeyConflict(const LauncherConfig& config, const ToyboxKey& key)
{
    const std::string name = ToyboxKeyName(config, key);
    for (const ToyboxKey& other : ToyboxKeys())
        if (&other != &key && SameKey(ToyboxKeyName(config, other), name))
            return true;
    for (const GameInfo& game : g_games)
        for (const GameKey& row : GameKeys(game.id))
            if (SameKey(GameKeyName(config, game.id, row), name))
                return true;
    return false;
}

namespace
{
    constexpr ToyboxPad PadShortcuts[] = { { "pad_wheel", N_("Swap wheel"), { 9, -1 }, 1 },
        { "pad_toybox", N_("Open the Toy Box"), { 9, 3 } }, { "pad_menu", N_("Game menu"), { 9, 8 } },
        { "pad_toybox_add", N_("Add a figure"), { -1, -1 } },
        { "pad_toybox_clear", N_("Clear the figures"), { -1, -1 } } };

    int PhysicalButton(std::string_view name)
    {
        for (int i = 0; i < PadButtonTotal; i++)
            if (name == PadNames[size_t(i)])
                return i;
        return -1;
    }
}

bool IsPadRemapLine(std::string_view line) { return line.starts_with("pad_") && PhysicalButton(line.substr(4)) >= 0; }

std::span<const ToyboxPad> ToyboxPads() { return PadShortcuts; }

std::array<int, 2> ToyboxPadValue(const LauncherConfig& config, const ToyboxPad& binding)
{
    const std::string* value = Find(config, "input", binding.line);
    if (value == nullptr)
        return binding.fallback;
    const size_t plus = value->find('+');
    if (binding.buttons == 1)
    {
        const int button = PhysicalButton(*value);
        return button < 0 ? binding.fallback : std::array<int, 2>{ button, -1 };
    }
    if (*value == "none")
        return { -1, -1 };
    if (plus == std::string::npos)
        return binding.fallback;
    const int first = PhysicalButton(std::string_view(*value).substr(0, plus));
    const int second = PhysicalButton(std::string_view(*value).substr(plus + 1));
    return first < 0 || second < 0 || first == second ? binding.fallback : std::array<int, 2>{ first, second };
}

void SetToyboxPad(LauncherConfig& config, const ToyboxPad& binding, std::array<int, 2> buttons)
{
    if (buttons[0] < 0 || buttons[0] >= PadButtonTotal ||
        (binding.buttons == 1 ? buttons[1] != -1
                              : buttons[1] < 0 || buttons[1] >= PadButtonTotal || buttons[0] == buttons[1]))
        return;
    std::string value = PadNames[size_t(buttons[0])];
    if (buttons[1] >= 0)
        value += std::string("+") + PadNames[size_t(buttons[1])];
    config["input"][binding.line] = value;
}

bool ToyboxPadOwn(const LauncherConfig& config, const ToyboxPad& binding)
{
    return Find(config, "input", binding.line) != nullptr;
}

void ResetToyboxPad(LauncherConfig& config, const ToyboxPad& binding) { Erase(config, "input", binding.line); }

std::string ToyboxPadName(const LauncherConfig& config, const ToyboxPad& binding, PadFamily family)
{
    const auto buttons = ToyboxPadValue(config, binding);
    if (buttons[0] < 0)
        return {};
    std::string name = PadButtonLabel(buttons[0], family);
    if (buttons[1] >= 0)
        name += " + " + PadButtonLabel(buttons[1], family);
    return name;
}

bool ToyboxPadConflict(const LauncherConfig& config, const ToyboxPad& binding)
{
    const auto buttons = ToyboxPadValue(config, binding);
    for (size_t i = 0; i < buttons.size(); i++)
    {
        const int button = buttons[i];
        if (button < 0 || (i == 0 && button == 9) || (i == 1 && buttons[0] == 9 && (button == 3 || button == 8)))
            continue;
        for (const GameInfo& game : g_games)
        {
            const int target = PadTarget(config, game.id, button);
            if (target >= 0)
                return true;
        }
    }
    return false;
}
