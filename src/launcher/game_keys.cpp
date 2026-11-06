#include "launcher/game_keys.h"
#include "launcher/i18n.h"
#include <array>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <optional>
#include <utility>

namespace
{
    constexpr std::array<const char*, 22> Actions = { "a", "b", "x", "y", "start", "back", "lb", "rb", "lt", "rt", "left_up", "left_down",
        "left_left", "left_right", "right_up", "right_down", "right_left", "right_right", "dpad_up", "dpad_down", "dpad_left", "dpad_right" };

    // The defaults, shared by the games: the pad's buttons sit on the same keys in each (A Space, X C, Y V, B X).
    // Names and buttons: each game's own controls screen, read in the running game (docs/settings.md, "Where the names come from").
    #define MOVE_ROWS \
        { "left_up", N_("Move up"), N_("L stick"), SDL_SCANCODE_W }, { "left_down", N_("Move down"), N_("L stick"), SDL_SCANCODE_S }, \
        { "left_left", N_("Move left"), N_("L stick"), SDL_SCANCODE_A }, { "left_right", N_("Move right"), N_("L stick"), SDL_SCANCODE_D }

    constexpr GameKey Ssa[] = {
        MOVE_ROWS,
        { "a", N_("1st Power / Accept"), "A", SDL_SCANCODE_SPACE, "Mouse Left" }, { "x", N_("2nd Power"), "X", SDL_SCANCODE_C, "Mouse Right" },
        { "y", N_("Use / Talk"), "Y", SDL_SCANCODE_V }, { "b", N_("Special Power / Back"), "B", SDL_SCANCODE_X },
        { "back", N_("Skylander Info"), "Back", SDL_SCANCODE_BACKSPACE }, { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
        { "right_up", N_("Shake / Insert / Swing"), N_("R stick up"), SDL_SCANCODE_I }, { "right_down", N_("Same"), N_("R stick down"), SDL_SCANCODE_K },
        { "right_left", N_("Same"), N_("R stick left"), SDL_SCANCODE_J }, { "right_right", N_("Same"), N_("R stick right"), SDL_SCANCODE_L },
    };
    constexpr GameKey Giants[] = {
        MOVE_ROWS,
        { "a", N_("Attack 1 / Accept"), "A", SDL_SCANCODE_SPACE, "Mouse Left" }, { "x", N_("Attack 2"), "X", SDL_SCANCODE_C, "Mouse Right" },
        { "y", N_("Action"), "Y", SDL_SCANCODE_V }, { "b", N_("Attack 3 / Back"), "B", SDL_SCANCODE_X },
        { "back", N_("Skylander Info"), "Back", SDL_SCANCODE_BACKSPACE }, { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
    };
    constexpr GameKey SwapForce[] = {
        MOVE_ROWS,
        { "a", N_("Jump"), "A", SDL_SCANCODE_SPACE }, { "x", N_("Attack 1"), "X", SDL_SCANCODE_C, "Mouse Left" },
        { "b", N_("Attack 2"), "B", SDL_SCANCODE_X, "Mouse Right" }, { "y", N_("Attack 3"), "Y", SDL_SCANCODE_V },
        { "rb", N_("Attack 2"), "RB", SDL_SCANCODE_E }, { "lb", N_("Attack 3"), "LB", SDL_SCANCODE_Q },
        { "lt", N_("Interact"), "LT", SDL_SCANCODE_1 }, { "rt", N_("Interact"), "RT", SDL_SCANCODE_3 },
        { "back", N_("Skylanders Stats"), "Back", SDL_SCANCODE_BACKSPACE }, { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
    };
    // LT is the game's tag team. RB fires an attack like B in the tutorial; LB fires nothing there (nor does Y), so it has no row.
    constexpr GameKey TrapTeam[] = {
        MOVE_ROWS,
        { "a", N_("Jump"), "A", SDL_SCANCODE_SPACE }, { "x", N_("Attack 1"), "X", SDL_SCANCODE_C, "Mouse Left" },
        { "b", N_("Attack 2"), "B", SDL_SCANCODE_X, "Mouse Right" }, { "y", N_("Attack 3"), "Y", SDL_SCANCODE_V },
        { "rb", N_("Attack 2"), "RB", SDL_SCANCODE_E },
        { "lt", N_("Switch Skylander / Villain"), "LT", SDL_SCANCODE_1 }, { "rt", N_("Interact"), "RT", SDL_SCANCODE_3 },
        { "back", N_("Skylander/Villain Stats"), "Back", SDL_SCANCODE_BACKSPACE }, { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
    };

    // SuperChargers' own controls screen (pause menu, read in the running game): the vehicle's words sit on the same buttons as
    // the Skylander's. LB drifts like A, LT switches the co-op driver, RB interacts; the D-pad (Customize) and the stick click
    // (Horn) have no row.
    constexpr GameKey SuperChargers[] = {
        MOVE_ROWS,
        { "a", N_("Jump / Drift (vehicle)"), "A", SDL_SCANCODE_SPACE }, { "x", N_("Attack 1"), "X", SDL_SCANCODE_C, "Mouse Left" },
        { "y", N_("Attack 2"), "Y", SDL_SCANCODE_V }, { "b", N_("Brake / Reverse (vehicle)"), "B", SDL_SCANCODE_X, "Mouse Right" },
        { "lb", N_("Drift (vehicle)"), "LB", SDL_SCANCODE_Q }, { "rb", N_("Interact"), "RB", SDL_SCANCODE_E },
        { "lt", N_("Switch Driver (co-op)"), "LT", SDL_SCANCODE_1 }, { "rt", N_("Accelerate (vehicle)"), "RT", SDL_SCANCODE_3 },
        { "back", N_("My Team"), "Back", SDL_SCANCODE_BACKSPACE }, { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
    };
    // Imaginators has no vehicles: SuperChargers' on-foot rows on the same engine's buttons (the rest of its pad is not read
    // yet, so it has no row); its Back is the Skylander profile.
    constexpr GameKey Imaginators[] = {
        MOVE_ROWS,
        { "a", N_("Jump"), "A", SDL_SCANCODE_SPACE }, { "x", N_("Attack 1"), "X", SDL_SCANCODE_C, "Mouse Left" },
        { "y", N_("Attack 2"), "Y", SDL_SCANCODE_V, "Mouse Right" }, { "rb", N_("Interact"), "RB", SDL_SCANCODE_E },
        { "back", N_("Skylander Profile"), "Back", SDL_SCANCODE_BACKSPACE }, { "start", N_("Pause"), "Start", SDL_SCANCODE_RETURN },
    };

    // The SDL names of the keys the launcher words in the player's language (the others, letters and digits, are the same everywhere).
    constexpr std::array<const char*, 16> KeyNames = { N_("Space"), N_("Return"), N_("Backspace"), N_("Escape"), N_("Up"), N_("Down"),
        N_("Left"), N_("Right"), N_("Left Shift"), N_("Right Shift"), N_("Left Ctrl"), N_("Right Ctrl"), N_("Left Alt"), N_("Right Alt"),
        N_("Caps Lock"), N_("Delete") };

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

    const GameKey* Row(std::string_view gameId, std::string_view action)
    {
        for (const GameKey& key : GameKeys(gameId))
            if (action == key.action)
                return &key;
        return nullptr;
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

std::string GameInputSection(std::string_view gameId) { return std::string(gameId) + ".input"; }

std::string GameKeyName(const LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    if (const std::string* value = Find(config, GameInputSection(gameId), KeyLine(key.action)))
        return *value;
    return SDL_GetScancodeName(key.fallback);
}

std::string KeyDisplayName(const std::string& sdlName)
{
    std::string shown = sdlName;
    if (SDL_Scancode place = SDL_GetScancodeFromName(sdlName.c_str()); place != SDL_SCANCODE_UNKNOWN)
        if (const char* name = SDL_GetKeyName(SDL_GetKeyFromScancode(place, SDL_KMOD_NONE, false)); name != nullptr && *name != 0)
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
    if (glyphs == "xbox360") return PadFamily::Xbox360;
    if (glyphs == "xboxone") return PadFamily::XboxOne;
    if (glyphs == "playstation") return PadFamily::PlayStation;
    if (glyphs == "nintendo") return PadFamily::Nintendo;
    if (glyphs == "xbox") return seen == PadFamily::XboxOne ? PadFamily::XboxOne : PadFamily::Xbox360;
    return seen;
}

std::string PadButtonName(const GameKey& key, PadFamily family) { return PadButtonName(std::string_view(key.pad), family); }

std::string PadButtonName(std::string_view pad, PadFamily family)
{
    // Per Xbox 360 name: Xbox One / Series, PlayStation 4 / 5, PlayStation 3, Nintendo Switch (by place: the 360's A is
    // the Switch's B). N_: the words are translated.
    static const std::pair<const char*, std::array<const char*, 4>> Names[] = {
        { "A", { "A", N_("Cross"), N_("Cross"), "B" } }, { "B", { "B", N_("Circle"), N_("Circle"), "A" } },
        { "X", { "X", N_("Square"), N_("Square"), "Y" } }, { "Y", { "Y", N_("Triangle"), N_("Triangle"), "X" } },
        { "Back", { "View", N_("Touchpad"), "Select", "-" } }, { "Start", { "Menu", "Options", "Start", "+" } },
        { "LB", { "LB", "L1", "L1", "L" } }, { "RB", { "RB", "R1", "R1", "R" } },
        { "LT", { "LT", "L2", "L2", "ZL" } }, { "RT", { "RT", "R2", "R2", "ZR" } },
        { "LS", { "LS", "L3", "L3", "LS" } }, { "RS", { "RS", "R3", "R3", "RS" } },
    };
    if (family != PadFamily::Xbox360)
    {
        size_t column = family == PadFamily::XboxOne ? 0 : family == PadFamily::PlayStation ? 1 : family == PadFamily::PlayStation3 ? 2 : 3;
        for (const auto& [xbox360, names] : Names)
            if (pad == xbox360)
                return i18n::Tr(names[column]);
    }
    // The 360's names as printed on the pad (Back stays Back, not the B button's "Back" action); the sticks' are words.
    return pad.find("stick") != std::string_view::npos || pad.starts_with("D-pad") ? i18n::Tr(std::string(pad).c_str()) : std::string(pad);
}

bool GameKeyConflict(const LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    const std::string name = GameKeyName(config, gameId, key);
    for (const GameKey& other : GameKeys(gameId))
        if (&other != &key && GameKeyName(config, gameId, other) == name)
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
}

void SeedGameInput(LauncherConfig& config, std::string_view gameId)
{
    const std::string section = GameInputSection(gameId);
    if (config.count(section) != 0 && config[section].count("seeded") != 0)
        return;
    auto shared = config.find("input");
    for (const char* action : Actions)
    {
        if (shared == config.end())
            break;
        if (auto value = shared->second.find(KeyLine(action)); value != shared->second.end())
            config[section].emplace(KeyLine(action), value->second); // a key the game already has stays
    }
    config[section]["seeded"] = "1"; // an empty copy is not written to the file: this is what says it was seeded
}

void ApplyGameInput(LauncherConfig& config, std::string_view gameId)
{
    const std::string section = GameInputSection(gameId);
    for (const char* action : Actions)
    {
        const std::string line = KeyLine(action);
        if (const std::string* value = Find(config, section, line))
            config["input"][line] = *value;
        else if (const GameKey* row = Row(gameId, action))
            config["input"][line] = SDL_GetScancodeName(row->fallback);
        else if (auto shared = config.find("input"); shared != config.end())
            shared->second.erase(line); // not one of this game's actions: the runtime's own default
    }
}

namespace
{
    // Display order = the runtime's g_padButtons; the 360's names, which the games' rows (GameKey::pad) use.
    constexpr std::array<const char*, PadButtonTotal> PadNames = { "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back", "ls", "rs",
        "dpad_up", "dpad_down", "dpad_left", "dpad_right" };
    constexpr std::array<const char*, PadButtonTotal> PadXbox = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Start", "Back", "LS", "RS",
        N_("D-pad up"), N_("D-pad down"), N_("D-pad left"), N_("D-pad right") };
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

int PadTarget(const LauncherConfig& config, std::string_view gameId, int button)
{
    const std::string* value = Find(config, GameInputSection(gameId), PadLine(button));
    if (value == nullptr)
        return button;
    for (int i = 0; i < PadButtonTotal; i++)
        if (*value == PadNames[size_t(i)])
            return i;
    return *value == "none" ? -1 : button;
}

void SetPadTarget(LauncherConfig& config, std::string_view gameId, int button, int target)
{
    const std::string section = GameInputSection(gameId);
    if (target == button)
    {
        if (auto entry = config.find(section); entry != config.end())
            entry->second.erase(PadLine(button));
        return;
    }
    config[section][PadLine(button)] = target < 0 ? "none" : PadNames[size_t(target)];
}

void ResetPadMap(LauncherConfig& config, std::string_view gameId)
{
    auto section = config.find(GameInputSection(gameId));
    if (section == config.end())
        return;
    for (int i = 0; i < PadButtonTotal; i++)
        section->second.erase(PadLine(i));
}

std::span<const char* const> RumbleLevels() { return Rumble; }

std::string RumbleLevel(const LauncherConfig& config, std::string_view gameId, int player)
{
    const std::string* value = Find(config, GameInputSection(gameId), player == 0 ? "rumble_p1" : "rumble_p2");
    for (const char* level : Rumble)
        if (value != nullptr && *value == level)
            return level;
    return "normal";
}

namespace
{
    // The runtime's mouse names (host/mouse.h), and how the player reads them.
    constexpr std::array<std::pair<const char*, const char*>, 7> MouseNames = { {
        { "Mouse Left", N_("Left click") }, { "Mouse Right", N_("Right click") }, { "Mouse Middle", N_("Middle click") },
        { "Mouse 4", N_("Back side button") }, { "Mouse 5", N_("Forward side button") }, { "Wheel Up", N_("Wheel up") },
        { "Wheel Down", N_("Wheel down") } } };

    bool IsMouseName(std::string_view name)
    {
        for (const auto& [stored, shown] : MouseNames)
            if (name == stored)
                return true;
        return false;
    }

    // What mouse_<action> says: the button, "" for none; nullopt when there is no line.
    std::optional<std::string> StoredMouse(const LauncherConfig& config, std::string_view gameId, const char* action)
    {
        auto section = config.find(GameInputSection(gameId));
        if (section == config.end())
            return std::nullopt;
        auto value = section->second.find(MouseLine(action));
        if (value == section->second.end())
            return std::nullopt;
        return IsMouseName(value->second) ? value->second : std::string();
    }

    constexpr ToyboxKey Toybox[] = { { "key_toybox", N_("Open the Toy Box"), "F6" }, { "key_toybox_add", N_("Add a figure"), "F7" },
        { "key_toybox_clear", N_("Clear the figures"), "F8" } };
}

bool GameMouseOn(const LauncherConfig& config, std::string_view gameId)
{
    const std::string* value = Find(config, GameInputSection(gameId), "mouse");
    return value != nullptr && (*value == "1" || *value == "true" || *value == "on" || *value == "yes");
}

void SetGameMouseOn(LauncherConfig& config, std::string_view gameId, bool on) { config[GameInputSection(gameId)]["mouse"] = on ? "1" : "0"; }

std::string GameMouseName(const LauncherConfig& config, std::string_view gameId, const GameKey& key)
{
    if (!GameMouseOn(config, gameId))
        return {};
    if (std::optional<std::string> stored = StoredMouse(config, gameId, key.action))
        return *stored;
    if (key.mouse == nullptr)
        return {};
    // The default goes when an action with a line of its own took the button (the runtime's mouse::Effective).
    for (const GameKey& other : GameKeys(gameId))
        if (&other != &key)
            if (std::optional<std::string> stored = StoredMouse(config, gameId, other.action); stored && *stored == key.mouse)
                return {};
    return key.mouse;
}

void SetGameMouseButton(LauncherConfig& config, std::string_view gameId, const GameKey& key, const std::string& name)
{
    const std::string section = GameInputSection(gameId);
    if (!name.empty())
        for (const GameKey& other : GameKeys(gameId))
            if (&other != &key && GameMouseName(config, gameId, other) == name)
                config[section][MouseLine(other.action)] = "none";
    config[section][MouseLine(key.action)] = name.empty() ? "none" : name;
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
