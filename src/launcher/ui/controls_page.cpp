// The keys, mouse and pad: a game's Keyboard and Controller tabs (its Settings) and, in the gear's Settings > Controls, the
// global layout every game inherits (game_keys.h): "All games" (the moves, Back, Pause: a Keyboard and a Controller
// column, stored in [input]), the mouse switch, the rumble and the Toy Box's keys. A game's own actions are set on its
// tabs only. On a game's tab a value the game does not set itself is dimmed; one it sets has a reset arrow
// (docs/settings.md, "The global layout and each game's keys").
#include "launcher/ui/launcher_app.h"
#include <fmt/format.h>

namespace launcher_ui
{
    namespace
    {
        namespace strings
        {
            constexpr const char* ResetKeys = N_("Reset to defaults");
            constexpr const char* ResetKeysHint = N_("Hold to reset every key of this game.");
            constexpr const char* ResetPadHint = N_("Hold to reset every button of this game.");
            constexpr const char* UseGlobal = N_("Use the global setting");
            constexpr const char* UseDefault = N_("Use the default");
            constexpr const char* Dimmed = N_("Dimmed values follow Launcher settings.");
            constexpr const char* GlobalIntro = N_("Games use these unless their own settings change them.");
            constexpr const char* Buttons = N_("Buttons");
            constexpr const char* StatsInfo = N_("Stats / Info"); // Back: each game names it its own way
        }

        enum class Hit
        {
            None,
            Pressed,
            Reset
        };

        constexpr const char* Dash = "\xE2\x80\x94";

        // The reset arrow's words: a shared row goes back to the global layout, a game's own row to its default.
        const char* ResetTip(bool own, bool shared)
        {
            return own ? Tr(shared ? strings::UseGlobal : strings::UseDefault) : nullptr;
        }

        // A key cap on the control column's right edge, the reset arrow before it.
        Hit KeyCapControl(const char* id, const char* shown, const ui::KeyButtonOptions& options, const char* resetTip)
        {
            const float capW = ui::KeyButtonWidth(shown, options);
            Hit hit = Hit::None;
            ImGui::PushID(id);
            if (resetTip != nullptr)
            {
                ui::AlignRight(ui::height::Normal + ui::space::XS + capW);
                if (ui::IconButton("##reset", ui::Icon::Reset, resetTip))
                    hit = Hit::Reset;
                ImGui::SameLine(0, ui::space::XS);
            }
            else
                ui::AlignRight(capW);
            if (ui::KeyButton("##key", shown, options))
                hit = Hit::Pressed;
            ImGui::PopID();
            return hit;
        }

        // A switch on the control column's right edge, the reset arrow before it.
        Hit ToggleControl(const char* id, bool* value, const char* resetTip)
        {
            const float toggleW = std::round(ui::height::Small * 1.75f);
            Hit hit = Hit::None;
            ImGui::PushID(id);
            if (resetTip != nullptr)
            {
                ui::AlignRight(ui::height::Small + ui::space::XS + toggleW);
                if (ui::IconButton("##reset", ui::Icon::Reset, resetTip, { .size = ui::Size::Small }))
                    hit = Hit::Reset;
                ImGui::SameLine(0, ui::space::XS);
            }
            if (ui::Toggle("##toggle", value))
                hit = Hit::Pressed;
            ImGui::PopID();
            return hit;
        }

        // A select filling the control column, the reset arrow before it. True when the select is open.
        bool SelectControl(const char* id, const char* preview, bool inherited, const char* resetTip, bool* reset)
        {
            ui::SelectOptions options;
            options.inherited = inherited;
            if (resetTip != nullptr)
            {
                const float avail = ImGui::GetContentRegionAvail().x;
                ui::AlignRight(avail);
                ImGui::PushID(id);
                *reset = ui::IconButton("##reset", ui::Icon::Reset, resetTip);
                ImGui::PopID();
                ImGui::SameLine(0, ui::space::XS);
                options.width = avail - ui::height::Normal - ui::space::XS;
            }
            return ui::BeginSelect(id, preview, options);
        }

        // A quiet dash as tall as a control: the column has nothing for that row. On the key caps' right edge, or where a
        // select's text starts.
        void NoControl(bool right)
        {
            ui::PushText(ui::TextStyle::Body);
            const float w = ImGui::CalcTextSize(Dash).x;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float x = right ? at.x + ImGui::GetContentRegionAvail().x - w - ui::space::S : at.x + ui::space::M;
            ui::DrawText(ImGui::GetWindowDrawList(),
                ImVec2(std::round(x), std::round(at.y + ui::height::Normal / 2 - ui::CapMiddle(ui::TextStyle::Body))),
                ui::Col(ui::color::Dim), Dash);
            ui::PopText();
            ImGui::Dummy(ImVec2(0, ui::height::Normal));
        }

        // What a key is for, in words: the right stick's directions are named by the stick (their rows say "Same").
        std::string Purpose(const GameKey& key, PadFamily family)
        {
            return std::string_view(key.action).starts_with("right_") ? PadButtonName(key, family)
                                                                      : std::string(Tr(key.label));
        }

        // An "All games" row's words: the game's, but Back's, which every game names differently.
        std::string GlobalPurpose(const GameKey& key)
        {
            return std::string_view(key.action) == "back" ? std::string(Tr(strings::StatsInfo))
                                                          : std::string(Tr(key.label));
        }

        const GameKey* FindKey(std::span<const GameKey> keys, std::string_view action)
        {
            for (const GameKey& key : keys)
                if (action == key.action)
                    return &key;
            return nullptr;
        }

        int PadIndex(std::string_view name)
        {
            const std::span<const char* const> buttons = PadButtons();
            for (size_t i = 0; i < buttons.size(); i++)
                if (name == buttons[i])
                    return int(i);
            return -1;
        }

        // "All games" rows: every game has them, so one game's list gives their keys and pad buttons.
        std::span<const GameKey> SharedKeys() { return GameKeys(g_games[0].id); }
    }

    // The movement (the left stick) as a cross of four keys, the right stick's as another, side by side: a title (the
    // action, the stick after it, a reset arrow when the game sets one of the four itself), then up over down, left and
    // right beside it.
    void App::DrawKeyCrosses(const std::string& game, std::span<const GameKey> keys)
    {
        const PadFamily family = PadFamilyNow();
        auto shown = [&](const GameKey& key) { return KeyDisplayName(GameKeyName(m_config, game, key)); };
        auto optionsOf = [&](const GameKey& key) {
            ui::KeyButtonOptions options;
            options.capturing = m_capturing == &key && m_captureGame == game;
            options.conflict = GameKeyConflict(m_config, game, key);
            options.inherited = !GameKeyOwn(m_config, game, key);
            return options;
        };
        auto keyCap = [&](const GameKey& key) {
            ui::KeyButtonOptions options = optionsOf(key);
            const std::string tip = Purpose(key, family);
            options.tooltip = options.conflict ? Tr("This key is used by another action") : tip.c_str();
            ImGui::PushID(key.action);
            if (ui::KeyButton("##key", shown(key).c_str(), options))
            {
                m_capturing = &key;
                m_captureGame = game;
            }
            ImGui::PopID();
        };
        auto capWidth = [&](const GameKey* key) { return ui::KeyButtonWidth(shown(*key).c_str(), optionsOf(*key)); };

        struct Cross
        {
            std::string title, note;
            const GameKey *up, *left, *down, *right;
        };
        std::vector<Cross> crosses;
        if (const GameKey* up = FindKey(keys, "left_up");
            up != nullptr && FindKey(keys, "left_down") && FindKey(keys, "left_left") && FindKey(keys, "left_right"))
            crosses.push_back({ Tr("Movement"), "(" + PadButtonName(*up, family) + ")", up, FindKey(keys, "left_left"),
                FindKey(keys, "left_down"), FindKey(keys, "left_right") });
        if (const GameKey* up = FindKey(keys, "right_up");
            up != nullptr && FindKey(keys, "right_down") && FindKey(keys, "right_left") && FindKey(keys, "right_right"))
            crosses.push_back({ Tr(up->label), "(" + std::string(Tr("R stick")) + ")", up, FindKey(keys, "right_left"),
                FindKey(keys, "right_down"), FindKey(keys, "right_right") });
        if (crosses.empty())
            return;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float columnW = ImGui::GetContentRegionAvail().x / 2;
        const float crossH = ui::type::Body + ui::space::S + 2 * ui::height::Normal + ui::space::XS;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < crosses.size(); i++)
        {
            const Cross& cross = crosses[i];
            const GameKey* four[] = { cross.up, cross.left, cross.down, cross.right };
            const float x = origin.x + float(i) * columnW;
            ui::PushText(ui::TextStyle::Body, true);
            ui::DrawText(draw, ImVec2(x, origin.y), ui::Col(ui::color::Ink), cross.title.c_str());
            const float titleEnd = x + ImGui::CalcTextSize(cross.title.c_str()).x;
            ui::PopText();
            ui::PushText(ui::TextStyle::Caption);
            ui::DrawText(draw,
                ImVec2(titleEnd + ui::space::XS,
                    origin.y + ui::Baseline(ui::TextStyle::Body, true) - ui::Baseline(ui::TextStyle::Caption)),
                ui::Col(ui::color::Dim), cross.note.c_str());
            const float noteEnd = titleEnd + ui::space::XS + ImGui::CalcTextSize(cross.note.c_str()).x;
            ui::PopText();
            bool own = false;
            for (const GameKey* key : four)
                own = own || GameKeyOwn(m_config, game, *key);
            if (const char* tip = ResetTip(own, SharedAction(cross.up->action)))
            {
                ImGui::SetCursorScreenPos(ImVec2(noteEnd + ui::space::XS,
                    std::round(origin.y + ui::CapMiddle(ui::TextStyle::Body, true) - ui::height::Small / 2)));
                ImGui::PushID(cross.up->action);
                if (ui::IconButton("##resetcross", ui::Icon::Reset, tip, { .size = ui::Size::Small }))
                {
                    for (const GameKey* key : four)
                        ClearGameKey(m_config, game, *key);
                    if (!SaveConfig())
                        Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
                }
                ImGui::PopID();
            }
            const float wL = capWidth(cross.left), wD = capWidth(cross.down), wU = capWidth(cross.up);
            const float top = origin.y + ui::type::Body + ui::space::S,
                        below = top + ui::height::Normal + ui::space::XS;
            // The keys' row starts on the title's edge; the top key over the middle one.
            const float middle = x + wL + ui::space::XS + wD / 2;
            ImGui::SetCursorScreenPos(ImVec2(std::round(middle - wU / 2), top));
            keyCap(*cross.up);
            ImGui::SetCursorScreenPos(ImVec2(x, below));
            keyCap(*cross.left);
            ImGui::SetCursorScreenPos(ImVec2(x + wL + ui::space::XS, below));
            keyCap(*cross.down);
            ImGui::SetCursorScreenPos(ImVec2(x + wL + wD + 2 * ui::space::XS, below));
            keyCap(*cross.right);
        }
        // A hairline (M below the keys) parts them from the list, like the list's rows from each other.
        const float line = origin.y + crossH + ui::space::M;
        draw->AddLine(ImVec2(origin.x, line + 0.5f), ImVec2(origin.x + ImGui::GetContentRegionAvail().x, line + 0.5f),
            ui::Col(ui::color::Hairline, 110.0f / 255), ui::stroke::Hairline);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, line + ui::stroke::Hairline));
    }

    // A key cap of one action: the game's (`game`), or the global layout's (empty); a press waits for a key.
    void App::DrawKeyCap(const std::string& game, const GameKey& key, const std::string& purpose)
    {
        const bool global = game.empty();
        const std::string shown =
            KeyDisplayName(global ? GlobalKeyName(m_config, key) : GameKeyName(m_config, game, key));
        const bool own = !global && GameKeyOwn(m_config, game, key);
        ui::KeyButtonOptions options;
        options.capturing = m_capturing == &key && m_captureGame == game;
        options.conflict = global ? GlobalKeyConflict(m_config, key) : GameKeyConflict(m_config, game, key);
        options.inherited = !global && !own;
        options.tooltip = options.conflict ? Tr("This key is used by another action") : purpose.c_str();
        switch (KeyCapControl(
            key.action, shown.c_str(), options, global ? nullptr : ResetTip(own, SharedAction(key.action))))
        {
        case Hit::Pressed:
            m_capturing = &key;
            m_captureGame = game;
            break;
        case Hit::Reset:
            ClearGameKey(m_config, game, key);
            if (!SaveConfig())
                Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            break;
        case Hit::None: break;
        }
    }

    // The other actions: a settings row each, named as the game names it, the pad's button after the name, the key at the
    // control column's edge.
    void App::DrawKeyRows(const std::string& game, std::span<const GameKey> keys)
    {
        const PadFamily family = PadFamilyNow();
        bool any = false;
        for (const GameKey& key : keys)
            any = any || (!std::string_view(key.action).starts_with("left_") &&
                             !std::string_view(key.action).starts_with("right_"));
        if (!any || !ui::BeginSettings(("##keys" + game).c_str()))
            return;
        for (const GameKey& key : keys)
        {
            const std::string_view name = key.action;
            if (name.starts_with("left_") || name.starts_with("right_"))
                continue;
            const std::string pad = "(" + PadButtonName(key, family) + ")";
            ui::SettingRow(Tr(key.label), nullptr, pad.c_str());
            DrawKeyCap(game, key, Purpose(key, family));
        }
        ui::EndSettings();
    }

    // Inside a settings table: a row per action with its mouse button (or the wheel), a dash for none.
    void App::DrawMouseRows(const std::string& game, std::span<const GameKey> keys)
    {
        const PadFamily family = PadFamilyNow();
        for (const GameKey& key : keys)
        {
            ui::SettingRow(Purpose(key, family).c_str(), nullptr, ("(" + PadButtonName(key, family) + ")").c_str());
            const bool capturing = m_mouseCapturing == &key && m_mouseCaptureGame == game;
            const std::string stored = GameMouseName(m_config, game, key);
            const std::string shown = capturing        ? std::string(Tr("Click or turn the wheel"))
                                      : stored.empty() ? std::string(Dash)
                                                       : MouseDisplayName(stored);
            const bool own = GameMouseButtonOwn(m_config, game, key);
            ui::KeyButtonOptions options;
            options.capturing = capturing;
            options.inherited = !own;
            options.tooltip = Tr("Click, then press a mouse button or turn the wheel (Escape cancels).");
            const std::string id = std::string("mouse_") + key.action;
            switch (KeyCapControl(id.c_str(), shown.c_str(), options, ResetTip(own, SharedAction(key.action))))
            {
            case Hit::Pressed:
                m_mouseCapturing = &key;
                m_mouseCaptureGame = game;
                m_mouseArmed = false;
                break;
            case Hit::Reset:
                ClearGameMouseButton(m_config, game, key);
                if (!SaveConfig())
                    Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
                break;
            case Hit::None: break;
            }
        }
    }

    // What one pad button does, as a select: the game's (`game`) or the global layout's (empty).
    void App::DrawPadSelect(const std::string& game, int button)
    {
        const bool global = game.empty();
        const PadFamily family = PadFamilyNow();
        // "B (Attack 3 / Back)": a button as the pad prints it, then the game's word for its job (for "All games" the
        // one every game shares).
        auto describe = [&](int other) {
            const std::string word =
                global ? (SharedPadButton(other) ? PadActionName(g_games[0].id, other) : std::string())
                       : PadActionName(game, other);
            const std::string name = PadButtonLabel(other, family);
            return word.empty() ? name : name + " (" + word + ")";
        };
        const int target = global ? GlobalPadTarget(m_config, button) : PadTarget(m_config, game, button);
        const std::string preview = target == button ? std::string(Tr("Default"))
                                    : target < 0     ? std::string(Tr("Nothing"))
                                                     : describe(target);
        const bool own = !global && PadOwn(m_config, game, button);
        bool reset = false;
        ImGui::PushID(button);
        if (SelectControl("##job", preview.c_str(), !global && !own,
                global ? nullptr : ResetTip(own, SharedPadButton(button)), &reset))
        {
            int chosen = -2;
            if (ui::SelectOption(Tr("Default"), target == button))
                chosen = button;
            for (int other = 0; other < PadButtonTotal; other++)
                if (other != button &&
                    ui::SelectOption((describe(other) + "##" + std::to_string(other)).c_str(), target == other))
                    chosen = other;
            if (ui::SelectOption(Tr("Nothing"), target < 0))
                chosen = -1;
            ui::EndSelect();
            if (chosen != -2)
            {
                if (global)
                    SetGlobalPadTarget(m_config, button, chosen);
                else
                    SetPadTarget(m_config, game, button, chosen);
                if (!SaveConfig())
                    Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            }
        }
        ImGui::PopID();
        if (reset)
        {
            SetPadTarget(m_config, game, button, GlobalPadTarget(m_config, button));
            if (!SaveConfig())
                Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
        }
    }

    // Inside a settings table: a row per pad button (its name, the game's word for its job after it, what it does now).
    void App::DrawPadRows(const std::string& game)
    {
        const PadFamily family = PadFamilyNow();
        for (int button = 0; button < PadButtonTotal; button++)
        {
            const std::string word = PadActionName(game, button);
            const std::string note = word.empty() ? std::string() : "(" + word + ")";
            const std::string name = PadButtonLabel(button, family);
            ui::SettingRow(name.c_str(), nullptr, note.empty() ? nullptr : note.c_str());
            DrawPadSelect(game, button);
        }
    }

    // Inside a settings table: the rumble of players 1 and 2 (`scope`: a game, empty for the global layout).
    void App::DrawRumbleRows(const std::string& scope)
    {
        const std::span<const char* const> levels = RumbleLevels();
        for (int player = 0; player < 2; player++)
        {
            ui::SettingRow(Tr(player == 0 ? N_("Player 1") : N_("Player 2")), nullptr);
            const std::string level =
                scope.empty() ? GlobalRumbleLevel(m_config, player) : RumbleLevel(m_config, scope, player);
            const char* preview = "";
            for (const Choice& choice : RumbleChoices)
                if (level == choice.value)
                    preview = Tr(choice.label);
            const bool own = !scope.empty() && RumbleOwn(m_config, scope, player);
            bool reset = false;
            ImGui::PushID(player);
            if (SelectControl("##rumble", preview, !scope.empty() && !own, ResetTip(own, true), &reset))
            {
                for (size_t i = 0; i < levels.size(); i++)
                    if (ui::SelectOption(Tr(RumbleChoices[i].label), level == levels[i]))
                    {
                        SetRumbleLevel(m_config, scope, player, levels[i]);
                        if (!SaveConfig())
                            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
                    }
                ui::EndSelect();
            }
            ImGui::PopID();
            if (reset)
            {
                SetRumbleLevel(m_config, scope, player, GlobalRumbleLevel(m_config, player));
                if (!SaveConfig())
                    Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            }
        }
    }

    // A game's keys and mouse ([<game>.input] over the global layout; game_keys.h).
    void App::DrawGameKeyboard()
    {
        const std::string& game = m_s->game.id;
        const std::span<const GameKey> keys = GameKeys(game);
        const PadFamily family = PadFamilyNow();
        const bool capturing = m_capturing != nullptr && m_captureGame == game;
        ui::Text(ui::TextStyle::Body,
            capturing ? Tf("Press a key for \"{}\" (Escape cancels)", Purpose(*m_capturing, family)).c_str()
                      : Tr("Choose a key to change it."),
            capturing ? ui::color::Ink : ui::color::Dim);
        ui::Text(ui::TextStyle::Caption, Tr(strings::Dimmed), ui::color::Dim);
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        DrawKeyCrosses(game, keys);
        DrawKeyRows(game, keys);

        // The mouse: a switch (off by default); once on, a button or the wheel for any action, attacks on the clicks by default.
        // Moving the mouse does nothing in the game.
        ui::SectionHeader(Tr("Mouse"), Tr("Left click: attack 1. Right click: attack 2. The game keeps the pointer."));
        if (ui::BeginSettings("##mouse"))
        {
            ui::SettingRow(Tr("Use the mouse"), nullptr);
            bool on = GameMouseOn(m_config, game);
            const Hit hit = ToggleControl("##mouseon", &on, ResetTip(GameMouseOwn(m_config, game), true));
            if (hit != Hit::None)
            {
                if (hit == Hit::Reset)
                    SetGameMouseOn(m_config, game, GlobalMouseOn(m_config));
                else
                    SetGameMouseOn(m_config, game, on);
                if (!SaveConfig())
                    Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
                m_mouseCapturing = nullptr;
            }
            if (GameMouseOn(m_config, game))
                DrawMouseRows(game, keys);
            ui::EndSettings();
        }

        ImGui::Dummy(ImVec2(0, ui::space::XS));
        ui::ButtonOptions reset;
        reset.kind = ui::ButtonKind::Danger;
        reset.hold = true;
        reset.tooltip = Tr(strings::ResetKeysHint);
        if (ui::Button(Tr(strings::ResetKeys), reset))
        {
            ResetGameKeys(m_config, game);
            if (!SaveConfig())
                Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            m_capturing = nullptr;
            m_mouseCapturing = nullptr;
            Toast(Tr("Keyboard bindings reset"), ui::ToastKind::Success);
        }
    }

    // A game's rumble and pad buttons ([<game>.input] over the global layout; game_keys.h).
    void App::DrawGameController()
    {
        const std::string& game = m_s->game.id;
        ui::Text(ui::TextStyle::Body, Tr("Choose what each button does."), ui::color::Dim);
        ui::Text(ui::TextStyle::Caption, Tr(strings::Dimmed), ui::color::Dim);
        ui::SectionHeader(Tr("Rumble"), nullptr, false);
        if (ui::BeginSettings("##rumble"))
        {
            DrawRumbleRows(game);
            ui::EndSettings();
        }
        ui::SectionHeader(Tr(strings::Buttons));
        if (ui::BeginSettings("##padmap"))
        {
            DrawPadRows(game);
            ui::EndSettings();
        }
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        ui::ButtonOptions reset;
        reset.kind = ui::ButtonKind::Danger;
        reset.hold = true;
        reset.tooltip = Tr(strings::ResetPadHint);
        if (ui::Button(Tr(strings::ResetKeys), reset))
        {
            ResetPadMap(m_config, game);
            if (!SaveConfig())
                Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            Toast(Tr("Controller buttons reset"), ui::ToastKind::Success);
        }
    }

    // Settings > Controls, under the controller choice: "All games" (a row per shared action, its key and its pad button
    // side by side, then the pad buttons no game names: the sticks' clicks, the D-pad), the mouse switch, the rumble and
    // the Toy Box's keys, all in [input].
    void App::DrawGlobalControls()
    {
        const PadFamily family = PadFamilyNow();
        const bool capturing = m_capturing != nullptr && m_captureGame.empty();
        ui::SectionHeader(Tr("All games"));
        ui::Text(ui::TextStyle::Caption,
            capturing ? Tf("Press a key for \"{}\" (Escape cancels)", GlobalPurpose(*m_capturing)).c_str()
                      : Tr(strings::GlobalIntro),
            capturing ? ui::color::Ink : ui::color::Dim);
        constexpr float KeyColumn = 170, PadColumn = 250;
        if (ui::BeginSettings("##allgames", KeyColumn, PadColumn))
        {
            ImGui::TableNextRow();
            for (int column = 1; column <= 2; column++)
            {
                ImGui::TableSetColumnIndex(column);
                const char* title = column == 1 ? Tr("Keyboard") : Tr("Controller");
                if (column == 1) // over the key caps' right edge; the pad's over its selects' text
                    ui::AlignRight(ui::TextSize(ui::TextStyle::Caption, title).x);
                else
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui::space::M);
                ui::Text(ui::TextStyle::Caption, title, ui::color::Dim);
            }
            const std::span<const GameKey> keys = SharedKeys();
            for (const char* action : { "left_up", "left_down", "left_left", "left_right", "back", "start" })
            {
                const GameKey* key = FindKey(keys, action);
                if (key == nullptr)
                    continue;
                const std::string pad = "(" + PadButtonName(*key, family) + ")";
                const std::string purpose = GlobalPurpose(*key);
                ui::SettingRow(purpose.c_str(), nullptr, pad.c_str());
                ImGui::PushID(action);
                DrawKeyCap("", *key, purpose);
                ImGui::TableSetColumnIndex(2);
                if (const int button = PadIndex(action); button >= 0)
                    DrawPadSelect("", button);
                else
                    NoControl(false); // the stick moves; only its click is a button
                ImGui::PopID();
            }
            for (int button = 0; button < PadButtonTotal; button++)
            {
                if (!SharedPadButton(button) || std::string_view(PadButtons()[size_t(button)]) == "back" ||
                    std::string_view(PadButtons()[size_t(button)]) == "start")
                    continue;
                const std::string name = PadButtonLabel(button, family);
                ui::SettingRow(name.c_str(), nullptr);
                NoControl(true);
                ImGui::TableSetColumnIndex(2);
                DrawPadSelect("", button);
            }
            ui::EndSettings();
        }

        ui::SectionHeader(Tr("Mouse"), Tr("Left click: attack 1. Right click: attack 2. The game keeps the pointer."));
        if (ui::BeginSettings("##globalmouse"))
        {
            ui::SettingRow(Tr("Use the mouse"), nullptr);
            bool on = GlobalMouseOn(m_config);
            if (ToggleControl("##mouseon", &on, nullptr) == Hit::Pressed)
            {
                SetGameMouseOn(m_config, "", on);
                if (!SaveConfig())
                    Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            }
            ui::EndSettings();
        }
        ui::SectionHeader(Tr("Rumble"));
        if (ui::BeginSettings("##globalrumble"))
        {
            DrawRumbleRows("");
            ui::EndSettings();
        }
        DrawToyboxKeys();
    }

    void App::DrawToyboxKeys()
    {
        ui::SectionHeader(Tr("Toy Box keys"));
        if (ui::BeginSettings("##toyboxkeys"))
        {
            // Each line: the keyboard's key, changed like the game's keys ([input] key_toybox..., shared by the games; Tab also
            // opens the Toy Box while its key is F6's default), then the pad's buttons ("View + Y": together) in the pad's own
            // pictures (the pad in use or chosen, else Xbox's), a dash where the pad has none (adding and clearing figures are
            // the keyboard's only). The pad's column ends on the control column's edge, the key before it.
            const std::span<const ToyboxKey> keys = ToyboxKeys();
            const std::vector<ui::PadButton> buttons[] = { { ui::PadButton::Back, ui::PadButton::North }, {}, {} };
            const ui::Glyphs glyphs = PadGlyphs();
            const float joint = ui::space::XS; // on each side of the "+"
            auto padWidth = [&](const std::vector<ui::PadButton>& pad) {
                float w = 0;
                for (size_t i = 0; i < pad.size(); i++)
                    w += ui::KeyCapWidth(pad[i], glyphs) +
                         (i > 0 ? 2 * joint + ui::TextSize(ui::TextStyle::Caption, "+").x : 0);
                return w;
            };
            float padColumn = 0;
            for (const auto& pad : buttons)
                padColumn = std::max(padColumn, padWidth(pad));
            for (size_t row = 0; row < keys.size(); row++)
            {
                const ToyboxKey& key = keys[row];
                ui::SettingRow(Tr(key.label), nullptr);
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImVec2 start = ImGui::GetCursorScreenPos();
                const float right = start.x + ImGui::GetContentRegionAvail().x, padX = right - padColumn;
                const float capH = ui::height::Normal,
                            middle = start.y + std::round(capH / 2); // the key button's middle
                const std::string name = KeyDisplayName(ToyboxKeyName(m_config, key));
                ui::KeyButtonOptions options;
                options.capturing = m_toyboxCapturing == &key;
                options.conflict = ToyboxKeyConflict(m_config, key);
                if (options.conflict)
                    options.tooltip = Tr("This key is used by another action");
                const float keyX =
                    padX - (padColumn > 0 ? ui::space::L : 0) - ui::KeyButtonWidth(name.c_str(), options);
                ImGui::SetCursorScreenPos(ImVec2(keyX, start.y));
                ImGui::PushID(key.line);
                if (ui::KeyButton("##toyboxkey", name.c_str(), options))
                    m_toyboxCapturing = &key;
                ImGui::PopID();
                float x = padX;
                if (!buttons[row].empty())
                    for (size_t i = 0; i < buttons[row].size(); i++)
                    {
                        if (i > 0)
                        {
                            ui::PushText(ui::TextStyle::Caption);
                            ui::DrawText(draw,
                                ImVec2(x + joint, std::round(middle - ui::CapMiddle(ui::TextStyle::Caption))),
                                ui::Col(ui::color::Dim), "+");
                            x += 2 * joint + ImGui::CalcTextSize("+").x;
                            ui::PopText();
                        }
                        x += ui::DrawKeyCap(draw, ImVec2(x, middle), buttons[row][i], glyphs).x;
                    }
                else // no pad button for it: a quiet dash in the pad's column
                {
                    ui::PushText(ui::TextStyle::Body);
                    ui::DrawText(draw,
                        ImVec2(std::round(padX + (padColumn - ImGui::CalcTextSize(Dash).x) / 2),
                            std::round(middle - ui::CapMiddle(ui::TextStyle::Body))),
                        ui::Col(ui::color::Dim), Dash);
                    ui::PopText();
                }
                ImGui::SetCursorScreenPos(ImVec2(keyX, start.y));
                ImGui::Dummy(ImVec2(right - keyX, capH));
            }
            ui::EndSettings();
        }
        if (m_toyboxCapturing != nullptr)
            ui::Text(ui::TextStyle::Caption,
                Tf("Press a key for \"{}\" (Escape cancels)", Tr(m_toyboxCapturing->label)).c_str(), ui::color::Ink);
    }
}
