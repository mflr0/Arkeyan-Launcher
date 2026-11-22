// A game's own settings (the Settings button on its Play page), in four tabs: Picture, Controls (Keyboard or Controller),
// Sound and language, and Manage (difficulty, disc, saves, shortcut, portable copy, folders, repair or uninstall). Arkeyan
// Launcher's own settings and what all games share (the controller, the global keys) are in the gear's Settings
// (launcher_ui.cpp, controls_page.cpp). The video keys are the game's own copy in [<game>.video] (game_settings.h); the
// keyboard and controller pages are in controls_page.cpp (game_keys.h).
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/window_size.h"
#include "launcher/env.h"
#include "launcher/shortcut.h"
#include "shared/collection/datadir.h"
#include <ctime>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <mutex>
#include <utility>
#include <imgui_internal.h>

namespace launcher_ui
{
    namespace
    {
        namespace strings
        {
            constexpr const char* GameText = N_("Game text");
            constexpr const char* GameTextHint = N_("Menus, subtitles and on-screen text.");
            constexpr const char* GameVoices = N_("Game voices");
            constexpr const char* GameVoicesHint = N_("Spoken lines and cutscenes.");
            constexpr const char* GameLanguage = N_("Game language");
            constexpr const char* GameLanguageHint = N_("Menus, subtitles, spoken lines and cutscenes.");
            constexpr const char* RestoreSaves = N_("Restore");
            constexpr const char* RestoreSavesHint = N_("Hold to restore; your current saves are kept first.");
            constexpr const char* Reinstall = N_("Reinstall");
            constexpr const char* ReinstallHint = N_("Hold to rebuild the game.");
            constexpr const char* ClearCachesHint = N_("Hold to clear the caches.");
            constexpr const char* DeleteData = N_("Delete saves and data");
            constexpr const char* DeleteDataHint = N_("Hold to delete saves, backups and caches.");
            constexpr const char* Uninstall = N_("Remove game");
            constexpr const char* UninstallHint = N_("Hold to delete the built game. Saves are kept.");
            constexpr const char* OpenFigures = N_("Figures");
        }

        std::string DiscFileName(const std::string& path)
        {
            return LauncherMiddleEllipsis(datadir::Text(datadir::FromText(path).filename()), 60);
        }

        // "12.3 GB" for what a button would free, nothing for less than a megabyte of noise.
        std::string Freed(uint64_t bytes)
        {
            return bytes == 0 ? std::string(Tr("nothing yet")) : ui::FormatSize(bytes);
        }

        // A save backup's folder name ("YYYYMMDD-HHMMSS", local time) as the time it was made.
        std::time_t StampTime(const std::string& stamp)
        {
            std::tm when{};
            int year = 0, month = 0, day = 0, hour = 0, minute = 0;
            if (std::sscanf(stamp.c_str(), "%4d%2d%2d-%2d%2d", &year, &month, &day, &hour, &minute) != 5)
                return 0;
            when.tm_year = year - 1900, when.tm_mon = month - 1, when.tm_mday = day, when.tm_hour = hour,
            when.tm_min = minute;
            when.tm_isdst = -1;
            return std::mktime(&when);
        }

        // "Today 09:30", "Yesterday 09:30", else the date and the time (backups come several a day).
        std::string BackupWhen(const std::string& stamp)
        {
            const std::time_t when = StampTime(stamp);
            const std::string text = ui::FormatWhen(when, std::time(nullptr));
            return text.find(':') != std::string::npos ? text : fmt::format("{}, {}", text, ui::FormatTime(when));
        }
    }

    // ---- The page ----------------------------------------------------------------------------------------------------

    void App::DrawGameSettings()
    {
        // A centred column, like the launcher's settings.
        const char* title = GameName(m_s->game.id, GameNameForm::Full);
        BeginSettingsPage("##gamesettingscolumn", title != nullptr ? title : m_s->game.name, true);

        static const char* const Tabs[] = { N_("Picture"), N_("Controls"), N_("Sound and language"), N_("Manage") };
        int tab = int(m_gameTab);
        DrawTabBar(Tabs, int(std::size(Tabs)), &tab);
        m_gameTab = GameTab(tab);
        BeginCard("##gamesettings", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        switch (m_gameTab)
        {
        case GameTab::Picture: DrawGameVideo(); break;
        case GameTab::Controls: DrawGameControls(); break;
        case GameTab::Sound:
            ui::SectionHeader(Tr("Sound"), nullptr, false);
            DrawGameSound();
            ui::SectionHeader(Tr("Language"));
            DrawGameLanguage();
            break;
        case GameTab::Manage: DrawGameManage(); break;
        }
        EndCard();
        if (m_testHdTex)
            ImGui::SetScrollHereY(1.0f); // the texture pack screenshots show the bottom of the Picture tab
        EndSettingsPage();
    }

    // Where a link lands: the tab, with its section selected or unfolded.
    void App::ShowGameTab(GameTab tab, GameSection section)
    {
        m_gameTab = tab;
        if (tab == GameTab::Controls && section != GameSection::None)
            m_gameControls = section;
        if (tab == GameTab::Manage)
        {
            m_gameManage = section;
            m_gameSectionScroll = section == GameSection::None ? 0 : 2;
        }
        m_testFocusView = -1; // two links can share a tab
    }

    void App::DrawGameControls()
    {
        const char* const pages[] = { Tr("Keyboard"), Tr("Controller") };
        int page = m_gameControls == GameSection::Controller ? 1 : 0;
        if (ui::Segmented("##controlspage", pages, &page))
        {
            m_gameControls = page == 1 ? GameSection::Controller : GameSection::Keyboard;
            m_capturing = nullptr; // a key waited for on the page that is left
        }
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        if (page == 1)
            DrawGameController();
        else
            DrawGameKeyboard();
    }

    // ---- Picture, sound and language --------------------------------------------------------------------------------------

    namespace
    {
        constexpr std::pair<int, int> WindowSizePresets[] = { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 },
            { 2560, 1440 } };

        // App's own flag is shared by every game; this one knows which game Custom was picked for.
        WindowSizeCustom g_windowSizeCustom;
    }

    bool IsPresetWindowSize(std::string_view value)
    {
        for (const auto& [w, h] : WindowSizePresets)
            if (value == fmt::format("{}x{}", w, h))
                return true;
        return false;
    }

    bool ShowsCustomWindowSize(WindowSizeCustom& state, std::string_view game, std::string_view value)
    {
        if (state.game != game)
            state = WindowSizeCustom{ std::string(game), false };
        int width = 0, height = 0;
        return WindowSizeOf(value, width, height) && (state.chosen || !IsPresetWindowSize(value));
    }

    // window_size (game_settings.h): Last used (no key), four sizes, or Custom's own width and height.
    void App::DrawWindowSize(const char* video)
    {
        const auto& Presets = WindowSizePresets;
        if (g_windowSizeCustom.game != m_s->game.id)
            m_windowSizeShown.clear(); // the fields are filled again from this game's value, not the last game's typing
        const std::string value = SettingGet(m_config, video, "window_size");
        int width = 0, height = 0;
        const bool sized = WindowSizeOf(value, width, height);
        int preset = -1;
        for (size_t i = 0; i < std::size(Presets); i++)
            if (value == fmt::format("{}x{}", Presets[i].first, Presets[i].second))
                preset = int(i);
        const bool custom = ShowsCustomWindowSize(g_windowSizeCustom, m_s->game.id, value);
        auto sizeText = [](int w, int h) { return Tf("{} x {}", w, h); };
        auto setSize = [&](int w, int h) { Set(video, "window_size", fmt::format("{}x{}", w, h)); };

        ui::SettingRow(Tr("Window size"), Tr("Last used: as it was when the game closed."));
        const std::string label = custom        ? std::string(Tr("Custom"))
                                  : preset >= 0 ? sizeText(Presets[preset].first, Presets[preset].second)
                                                : std::string(Tr("Last used"));
        if (ui::BeginSelect("##windowsize", label.c_str()))
        {
            if (ui::SelectOption(Tr("Last used"), !sized))
            {
                g_windowSizeCustom.chosen = false;
                m_config[video].erase("window_size");
                if (!SaveConfig())
                    Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            }
            for (size_t i = 0; i < std::size(Presets); i++)
                if (ui::SelectOption(
                        fmt::format("{}##size{}", sizeText(Presets[i].first, Presets[i].second), i).c_str(),
                        !custom && preset == int(i)))
                {
                    g_windowSizeCustom.chosen = false;
                    setSize(Presets[i].first, Presets[i].second);
                }
            if (ui::SelectOption(Tr("Custom"), custom))
            {
                g_windowSizeCustom.chosen = true;
                if (!sized)
                    setSize(Presets[0].first, Presets[0].second);
            }
            ui::EndSelect();
        }
        if (!custom)
            return;

        // Typed digits are saved once the number reaches the minimum; until then the field keeps what is typed.
        const std::string now = SettingGet(m_config, video, "window_size");
        if (now != m_windowSizeShown && WindowSizeOf(now, width, height))
        {
            m_windowSizeShown = now;
            snprintf(m_windowSizeText[0], sizeof m_windowSizeText[0], "%d", width);
            snprintf(m_windowSizeText[1], sizeof m_windowSizeText[1], "%d", height);
        }
        ui::TextFieldOptions digits;
        digits.flags = ImGuiInputTextFlags_CallbackCharFilter;
        digits.callback = [](ImGuiInputTextCallbackData* data) {
            return data->EventChar >= '0' && data->EventChar <= '9' ? 0 : 1;
        };
        const std::string widthHint = Tf("{} or more", MinWindowWidth), heightHint = Tf("{} or more", MinWindowHeight);
        ui::SettingRow(Tr("Width"), widthHint.c_str());
        if (ui::TextField("##windowwidth", m_windowSizeText[0], sizeof m_windowSizeText[0], digits))
            if (int typed = std::atoi(m_windowSizeText[0]); typed >= MinWindowWidth)
                setSize(typed, height);
        ui::SettingRow(Tr("Height"), heightHint.c_str());
        if (ui::TextField("##windowheight", m_windowSizeText[1], sizeof m_windowSizeText[1], digits))
            if (int typed = std::atoi(m_windowSizeText[1]); typed >= MinWindowHeight)
                setSize(width, typed);
    }

    void App::DrawGameVideo()
    {
        const std::string videoSection = GameVideoSection(m_s->game.id);
        const char* video = videoSection.c_str();
        auto toggle = [&](const char* id, const char* key, const char* fallback) {
            bool value = ParseBool(SettingGet(m_config, video, key, fallback));
            if (ui::Toggle(id, &value))
                Set(video, key, value ? "1" : "0");
        };

        // The window and the pace.
        ui::SectionHeader(Tr("Display"), nullptr, false);
        if (ui::BeginSettings("##display"))
        {
            ui::SettingRow(Tr("Fullscreen"), nullptr);
            toggle("##fullscreen", "fullscreen", "0");
            if (!ParseBool(SettingGet(m_config, video, "fullscreen", "0")))
                DrawWindowSize(video);

            ui::SettingRow(
                Tr("Widescreen"), Tr("Auto shows more when wide; Fill covers a taller screen; 16:9 adds bars."));
            SettingSelect("##aspect", video, "aspect", AspectChoices, 0);

            ui::SettingRow(Tr("Frame rate limit"), Tr("Made for 30 FPS; 60 is experimental."));
            const std::span<const Choice> fpsChoices = FrameRateChoices(m_s->game.id);
            int fpsDefault = 0;
            for (size_t i = 0; i < fpsChoices.size(); i++)
                if (std::string_view(fpsChoices[i].value) == DefaultFrameRate(m_s->game.id))
                    fpsDefault = int(i);
            SettingSelect("##fps", video, "fps_limit", fpsChoices, fpsDefault);

            const std::string fpsLimit = SettingGet(m_config, video, "fps_limit", DefaultFrameRate(m_s->game.id));
            if (std::string_view(fpsChoices[size_t(ChoiceIndex(fpsChoices, fpsLimit, fpsDefault))].value) == "60")
            {
                ui::SettingRow(
                    Tr("Drop to 30 FPS when needed"), Tr("Off: your rate stays, even if the picture stutters."));
                // Unset, the game chooses from its GPU at its next start and writes it (on for integrated and Apple
                // GPUs, off for discrete ones); until then the page shows the likelier choice.
#if defined(__APPLE__)
                constexpr const char* GpuDefault = "1";
#else
                constexpr const char* GpuDefault = "0";
#endif
                const std::string fallbackDefault = SettingGet(m_config, "video", "fps_auto_fallback", GpuDefault);
                toggle("##fpsautofallback", "fps_auto_fallback", fallbackDefault.c_str());
                // Offscreen page check: feed the switch through ImGui, then capture the rendered page.
                if (!m_shots.empty() && LauncherEnv("SKY_LAUNCHER_TEST_FPS_FALLBACK") != nullptr)
                {
                    static int stage = 0;
                    static uint64_t captureAt = 0;
                    m_testAt = SDL_GetTicks() + 60000;
                    if (stage == 0)
                    {
                        ImGui::ActivateItemByID(ImGui::GetItemID());
                        captureAt = SDL_GetTicks() + 300;
                        stage++;
                    }
                    else if (SDL_GetTicks() >= captureAt)
                    {
                        Screenshot(("fps-fallback-" + std::string(m_s->game.id)).c_str());
                        m_running = false;
                    }
                }
            }

            ui::SettingRow(Tr("VSync"), Tr("No tearing, a little more latency."));
            toggle("##vsync", "vsync", "1");

            ui::SettingRow(Tr("FPS counter"), Tr("The frame rate in a corner of the game (F3 in game)."));
            toggle("##showfps", "show_fps", "0");

            ui::SettingRow(Tr("Skip intro movies"), nullptr);
            toggle("##skipintromovies", "skip_intro_movies", "0");

            if (GpuBackendShown)
            {
                // gpu_backend_note: the game switched itself to Vulkan when Direct3D 12 showed nothing (docs/settings.md).
                const bool switched = SettingGet(m_config, video, "gpu_backend") == "vulkan" &&
                                      !SettingGet(m_config, video, "gpu_backend_note").empty();
                ui::SettingRow(Tr("Graphics API"),
                    switched ? Tr("Vulkan, since Direct3D 12 showed nothing on this PC.")
                             : Tr("Direct3D 12 by default; the game uses the other if it cannot start."));
                if (SettingSelect("##gpubackend", video, "gpu_backend", GpuBackendChoices, 0) &&
                    SettingGet(m_config, video, "gpu_backend") != "vulkan")
                {
                    m_config[video].erase("gpu_backend_note");
                    if (!SaveConfig())
                        Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
                }
            }
            // Offscreen page check (SKY_LAUNCHER_TEST_GPU_BACKEND=<value>): the row's write where it is shown, then the page.
            if (!m_shots.empty())
                if (const char* chosen = LauncherEnv("SKY_LAUNCHER_TEST_GPU_BACKEND"))
                {
                    static uint64_t captureAt = 0;
                    if (captureAt == 0)
                    {
                        if (GpuBackendShown)
                            Set(video, "gpu_backend", chosen);
                        captureAt = SDL_GetTicks() + 300;
                        m_testAt = SDL_GetTicks() + 60000;
                    }
                    else if (SDL_GetTicks() >= captureAt)
                    {
                        Screenshot(("gpu-backend-" + std::string(m_s->game.id)).c_str());
                        m_running = false;
                    }
                }
            ui::EndSettings();
        }

        // What the 3D scene is drawn at, and how it is smoothed.
        ui::SectionHeader(Tr("Image quality"));
        if (ui::BeginSettings("##imagequality"))
        {
            std::string scaleHint = Tr("Higher scales need a dedicated graphics card.");
            if (int scale = ResolutionScaleOf(SettingGet(m_config, video, "resolution_scale")); scale >= 1)
                scaleHint += "\n" + Tf("{} x {} for the 3D scene", 1120 * scale, 704 * scale);
            ui::SettingRow(Tr("Resolution scale"), scaleHint.c_str());
            SettingSelect("##scale", video, "resolution_scale", ScaleChoices, 0);

            // FSR 3 (upscaler=fsr3, else off) and its preset, shown while it is on; the game's F3 menu changes both too.
            ui::SettingRow(Tr("FSR 3"), Tr("AMD upscaling: faster, nearly as sharp (F3 in game)."));
            bool fsr = SettingGet(m_config, video, "upscaler", "off") == "fsr3";
            if (ui::Toggle("##upscaler", &fsr))
                Set(video, "upscaler", fsr ? "fsr3" : "off");
            if (fsr)
            {
                ui::SettingRow(Tr("FSR 3 quality"), Tr("Native AA is the sharpest, Ultra Performance the fastest."));
                SettingSelect("##fsrquality", video, "fsr_quality", FsrQualityChoices, 1); // Quality
                // FSR 3 frame generation: needs the upscaler, so shown with it, where the game can run it. Greyed and off until
                // it renders as it should (0.9.4): the game ignores fsr_frame_generation meanwhile, its F3 row says the same.
                if (FrameGenerationOffered(m_s->game.id))
                {
                    const std::string hint =
                        std::string(Tr("Doubles the frames shown; a little more input delay (F3 in game).")) + "\n" +
                        Tr("Coming later");
                    ui::SettingRow(Tr("Frame generation"), hint.c_str());
                    bool frameGeneration = false;
                    ui::Toggle("##framegen", &frameGeneration, true);
                }
            }

            if (m_s->game.id == std::string_view("swapforce"))
            {
                ui::SettingRow(Tr("Original resolution"), Tr("The 3D scene at the console's size."));
                toggle("##originalresolution", "original_resolution", "0");
            }

            ui::SettingRow(Tr("Texture filtering"), Tr("Keeps textures sharp at an angle."));
            SettingSelect("##filter", video, "anisotropy", FilterChoices, 0);

            // The games whose FXAA pass the runtime knows. Not chosen, the runtime picks Gentle from resolution scale 2 (the automatic
            // scale is 2 on Apple silicon and discrete GPUs, 1 on integrated ones: the page shows Gentle, the runtime may use Game there).
            if (m_s->game.id == std::string_view("swapforce") || m_s->game.id == std::string_view("trapteam"))
            {
                ui::SettingRow(Tr("Edge smoothing"), Tr("FXAA also softens texture detail; Gentle keeps more of it."));
                SettingSelect("##edgesmoothing", video, "edge_smoothing", EdgeSmoothingChoices,
                    ResolutionScaleOf(SettingGet(m_config, video, "resolution_scale")) == 1 ? 0 : 1);
            }

            ui::EndSettings();
        }

        // The game's HD textures (made from the disc). PLAY still passes texture_packs=0 and texture_dump=0 (ApplyGameVideo).
        ui::SectionHeader(Tr("Textures"));
        if (ui::BeginSettings("##textures"))
        {
            DrawHdTextures();
            ui::EndSettings();
        }
        DrawHdStatus();
    }

    // The game's sound: [<game>.audio], which the game reads after [audio].
    void App::DrawGameSound()
    {
        if (!ui::BeginSettings("##sound"))
            return;
        const std::string audioSection = std::string(m_s->game.id) + ".audio";
        ui::SettingRow(Tr("Volume"), Tr("The game's volume (F3 in game)."));
        SettingSelect("##volume", audioSection.c_str(), "volume", VolumeChoices, 10);

        ui::SettingRow(Tr("Speakers"), Tr("Auto: 5.1 when the device has six channels."));
        SettingSelect("##speakers", audioSection.c_str(), "speakers", SpeakerChoices, 0);

        // Trap Team's portal has a speaker for the traps' voices; the game reads the switch from [trapteam.audio].
        if (m_s->game.id == std::string_view("trapteam"))
        {
            ui::SettingRow(Tr("Portal sound"),
                Tr("The traps' voices: a Traptanium portal plays them, else the computer (F3 in game)."));
            bool portalSound = ParseBool(SettingGet(m_config, audioSection.c_str(), "portal_sound", "1"));
            if (ui::Toggle("##portalsound", &portalSound))
                Set(audioSection.c_str(), "portal_sound", portalSound ? "1" : "0");
        }
        ui::EndSettings();
    }

    void App::DrawGameLanguage()
    {
        if (!ui::BeginSettings("##language"))
            return;
        // Trap Team's voices come with the text (its language packs hold both): one choice for the two.
        const bool together = m_s->game.id == std::string_view("trapteam");
        ui::SettingRow(Tr(together ? strings::GameLanguage : strings::GameText),
            Tr(together ? strings::GameLanguageHint : strings::GameTextHint));
        // Nothing chosen: the launcher's own language when the disc holds it, as the game is then started in it (game_run.cpp).
        auto fallback = [](const std::vector<Choice>& choices) {
            return std::max(0, ChoiceIndex(choices, i18n::Current(), 0));
        };
        const std::vector<Choice> text = GameLanguageChoices(m_config, m_s->game.id, false);
        SettingSelect("##text", m_s->game.id, "language", text, fallback(text));
        if (!together)
        {
            ui::SettingRow(Tr(strings::GameVoices), Tr(strings::GameVoicesHint));
            const std::vector<Choice> voice = GameLanguageChoices(m_config, m_s->game.id, true);
            SettingSelect("##voice", m_s->game.id, "voice_language", voice, fallback(voice));
        }
        ui::EndSettings();
    }

    // ---- Disc --------------------------------------------------------------------------------------------------------

    // The game forgets its disc image: PLAY asks for one again. The file, the built game and the saves are not touched.
    void App::ForgetDisc()
    {
        m_s->verify.Join();
        m_s->chosen.clear();
        m_s->disc = DiscState::None;
        m_s->discPath.clear();
        m_s->discSize = 0;
        m_s->check = {};
        m_s->discChecked.reset();
        m_s->discCheckWanted = false;
        m_s->installView = m_s->kitUsed = false;
        m_config[m_s->game.id].erase("iso");
        StoreDiscLanguages(m_config, m_s->game.id, {});
        if (!SaveConfig())
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
        RefreshInstall();
    }

    void App::DrawGameDisc()
    {
        if (m_s->fromCopy)
        {
            // The game plays from its copied game data: the image is not used, so its buttons are not offered.
            const std::string iso = SettingGet(m_config, m_s->game.id, "iso");
            ui::Text(ui::TextStyle::Body, iso.empty() ? Tr("None chosen.") : DiscFileName(iso).c_str());
            ui::Text(ui::TextStyle::Caption, Tr("Not needed: the game plays from its copied data."), ui::color::Dim);
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            DrawDiscCheck(); // of the copy
            DrawCheckAgain();
        }
        else
        {
            ui::Text(ui::TextStyle::Body,
                m_s->discPath.empty() ? Tr("None chosen yet.") : DiscFileName(m_s->discPath).c_str());
            const GameInfo* other = nullptr;
            switch (m_s->disc)
            {
            case DiscState::Ready: DrawDiscCheck(); break;
            case DiscState::Verifying: ui::StatusPill(ui::Status::Busy, (m_s->verify.Stage() + "...").c_str()); break;
            case DiscState::Failed:
                ui::StatusPill(ui::Status::Error, SlotStatus(m_s).text.c_str());
                ui::Text(ui::TextStyle::Body, m_s->check.message.c_str(), ui::color::InkSoft);
                if (m_s->check.status == GameSourceStatus::WrongGame)
                    other = FindEnabledGame(m_s->check.otherGame, m_config);
                break;
            case DiscState::None: break;
            }
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            const bool busy = Installing() || m_s->disc == DiscState::Verifying || m_s->copyJob.Running();
            if (other != nullptr)
            {
                ui::ButtonOptions use;
                use.kind = ui::ButtonKind::Primary;
                use.disabled = busy;
                if (ui::Button(Tf("Use it for {}", other->name).c_str(), use))
                    UseDiscForOtherGame();
                ImGui::SameLine(0, ui::space::S);
            }
            ui::ButtonOptions remove;
            remove.disabled = busy || m_s->discPath.empty();
            if (ui::Button(Tr("Remove"), remove))
                ForgetDisc();
            ImGui::SameLine(0, ui::space::S);
            ui::ButtonOptions folder;
            folder.icon = ui::Icon::Folder;
            folder.disabled = busy || m_s->discPath.empty();
            if (ui::Button(Tr("Show in folder"), folder))
            {
                std::filesystem::path parent =
                    std::filesystem::path(std::u8string(m_s->discPath.begin(), m_s->discPath.end())).parent_path();
                if (!OpenFolder(parent))
                    Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
            }
            ui::Text(ui::TextStyle::Caption, Tr("The file stays where it is."), ui::color::Dim);
            DrawCheckAgain();
        }
        DrawGameData();
    }

    // Reads the verified disc again (its kept result forgotten): after the drive had trouble, or a copy was repaired.
    void App::DrawCheckAgain()
    {
        if (m_s->disc != DiscState::Ready)
            return;
        ImGui::Dummy(ImVec2(0, ui::space::XXS));
        ui::ButtonOptions again;
        again.disabled = DiscChecking(*m_s) || Installing() || m_s->copyJob.Running();
        if (ui::Button(Tr("Check again"), again))
            WantDiscCheck(true);
    }

    // ---- Saves -------------------------------------------------------------------------------------------------------

    // The game's save backups (save_backup.h): one row per backup (date, time, size) and the button that puts it back.
    // The game keeps them itself before it overwrites a save; restoring one first keeps the saves the player has now.
    void App::RefreshSaveBackups()
    {
        m_s->saveBackups = savebackup::List(savebackup::Root(m_s->game.id));
        m_s->saveBackupsLoaded = true;
    }

    void App::RestoreSaveBackup(const std::string& stamp)
    {
        const savebackup::Restore restore = savebackup::RestoreBackup(savebackup::Root(m_s->game.id), stamp);
        m_s->saveBackupsLoaded = false; // the restore made a backup of its own
        if (restore.ok)
            Toast(Tf("Saves restored from {}", BackupWhen(stamp)), ui::ToastKind::Success);
        else if (restore.error == "the game is running")
            Toast(Tr("Quit the game before restoring a backup"), ui::ToastKind::Error);
        else
            Toast(Tf("Couldn't restore the backup: {}", restore.error), ui::ToastKind::Error);
    }

    void App::DrawSaveBackups()
    {
        if (!m_s->saveBackupsLoaded)
            RefreshSaveBackups();
        const bool running =
            savebackup::GameRunning(savebackup::Root(m_s->game.id)) || m_session.Running() == m_s->game.id;
        ui::Text(ui::TextStyle::Caption,
            running ? Tr("The game is running: quit it to restore a backup.") : Tr("Earlier versions of your saves."),
            ui::color::Dim);
        ui::ButtonOptions folder;
        folder.icon = ui::Icon::Folder;
        if (ui::Button(Tr("Show in folder"), folder))
        {
            std::filesystem::path saves = savebackup::Root(m_s->game.id);
            std::error_code error;
            std::filesystem::create_directories(saves, error);
            if (!OpenFolder(saves))
                Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
        }
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        if (m_s->saveBackups.empty())
        {
            ui::EmptyState(ui::Icon::Folder, Tr("No backups yet."));
            return;
        }

        // Five rows at a time; the newest first. Each: when, how many files, the size, and the hold button that puts it back.
        ui::ButtonOptions restore;
        restore.kind = ui::ButtonKind::Danger;
        restore.hold = true;
        restore.disabled = running;
        restore.tooltip = Tr(strings::RestoreSavesHint);
        const float rowHeight =
            ui::height::Large + ui::space::XXS + ImGui::GetStyle().ItemSpacing.y; // a row and the gap after it
        ui::BeginRoomyChild("##savebackuplist",
            ImVec2(0, std::min(5 * rowHeight, rowHeight * float(m_s->saveBackups.size()))),
            ImGuiChildFlags_NavFlattened);
        const float buttonWidth = ui::ButtonWidth(Tr(strings::RestoreSaves), restore);
        for (const savebackup::Entry& backup : m_s->saveBackups)
        {
            ImGui::PushID(backup.stamp.c_str());
            const float top = ImGui::GetCursorPosY();
            const std::string when = BackupWhen(backup.stamp), size = ui::FormatSize(backup.bytes);
            const std::string files =
                backup.files == 1 ? std::string(Tr("One file")) : Tf("{} files", ui::FormatNumber(backup.files));
            ui::ListRowOptions row;
            row.subtitle = files.c_str();
            row.trailing = size.c_str();
            row.width = ImGui::GetContentRegionAvail().x - buttonWidth - ui::space::S;
            ui::ListRow(when.c_str(), row);
            ImGui::SameLine(0, ui::space::S);
            ImGui::SetCursorPosY(top + (ui::height::Large - ui::height::Normal) / 2);
            if (ui::Button(Tr(strings::RestoreSaves), restore))
                RestoreSaveBackup(backup.stamp);
            ImGui::SetCursorPosY(top + ui::height::Large);
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            ImGui::PopID();
        }
        ui::EndRoomyChild();
    }

    // ---- Game: shortcut, folders, reinstall, delete data, uninstall -----------------------------------------------------

    void App::RefreshDataSizes()
    {
        m_s->dataSizes = MeasureGameData(FoldersOf(m_s->game.id));
        m_s->dataSizesLoaded = true;
    }

    void App::RunDataAction(const DataResult& result, const char* done) // `done`: an English string, translated here
    {
        m_s->saveBackupsLoaded = m_s->dataSizesLoaded = false;
        Toast(result.ok ? Tf("{}: {} freed", Tr(done), ui::FormatSize(result.freed)) : result.error,
            result.ok ? ui::ToastKind::Success : ui::ToastKind::Error);
    }

    void App::RunUninstall(const DataResult& result)
    {
        if (!SaveConfig())
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
        m_s->installed = {};
        RefreshInstall();
        RunDataAction(result, N_("Game uninstalled"));
    }

    namespace
    {
        // The folder dialog answers on a thread of its own: the folder waits here for the window's next frame.
        std::mutex g_portableMutex;
        std::string g_portableFolder;

        void SDLCALL PortableFolderChosen(void*, const char* const* folders, int)
        {
            std::lock_guard lock(g_portableMutex);
            if (folders != nullptr && folders[0] != nullptr)
                g_portableFolder = folders[0];
        }

        constexpr const char* SystemName()
        {
#if defined(_WIN32)
            return "Windows";
#elif defined(__APPLE__)
            return "macOS";
#else
            return "Linux";
#endif
        }
    }

    // The game in one file for this system (portable_export.h): the player reads what it is and chooses a folder, then it is
    // written in the background.
    void App::DrawPortableExport()
    {
        {
            std::lock_guard lock(g_portableMutex);
            if (!g_portableFolder.empty())
                StartPortableExport(datadir::FromText(std::exchange(g_portableFolder, std::string())));
        }
        const bool built = !m_s->installed.runtime.empty(), ready = built && m_s->disc == DiscState::Ready;
        const bool running = m_s->portableJob.Running();
        std::string hint = !built   ? Tr("Install the game first.")
                           : !ready ? Tr("Choose the disc image first.")
                                    : Tr("The game in one file: it plays without the launcher or the disc image.");
        if (running)
        {
            const uint64_t total = m_s->portableJob.Total();
            hint = total == 0
                       ? std::string(Tr("Exporting..."))
                       : Tf("Exporting: {} of {}", ui::FormatSize(m_s->portableJob.Bytes()), ui::FormatSize(total));
        }
        else if (!m_s->portableMessage.empty())
            hint = m_s->portableMessage;
        ui::SettingRow(Tr("Portable copy"), hint.c_str());
        if (running)
        {
            ui::ProgressBar(m_s->portableJob.Fraction(), -1, true);
            ui::ButtonOptions stop;
            stop.icon = ui::Icon::Stop;
            ui::AlignRight(ui::ButtonWidth(Tr("Stop"), stop));
            if (ui::Button(Tr("Stop"), stop))
                m_s->portableJob.Cancel();
            return;
        }
        ui::ButtonOptions exportOptions;
        exportOptions.icon = ui::Icon::ArrowUp; // out of the launcher, as every export
        exportOptions.disabled = !ready || Installing();
        const std::string label = Tf("Export for {}", SystemName());
        const bool saved = !m_s->portablePath.empty();
        ui::AlignRight(ui::ButtonWidth(label.c_str(), exportOptions) + (saved ? ui::space::S + ui::height::Normal : 0));
        if (ui::Button(label.c_str(), exportOptions))
        {
            m_portableAsk = true;
            OpenPortableExport();
        }
        if (saved)
        {
            ImGui::SameLine(0, ui::space::S);
            if (ui::IconButton("##portablefolder", ui::Icon::Folder, Tr("Show in folder")) &&
                !OpenFolder(m_s->portablePath.parent_path()))
                Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
        }
    }

    void App::DrawPortableDialog()
    {
        const bool ready = !m_s->installed.runtime.empty() && m_s->disc == DiscState::Ready;
        if (m_s->portableJob.Running())
            return;

        // What the copy is, before anything is written: only for a game that is installed and has its disc (not "Up to 0 bytes").
        if (!ready)
            m_portableAsk = false;
        if (m_portableAsk &&
            ui::BeginModal("##portable", Tr("Export a portable copy"), &m_portableAsk, ui::ModalSize::Medium))
        {
            ui::Text(ui::TextStyle::Body,
                Tr("This copy holds your game. It is for your personal use only: do not share it."), ui::color::Ink);
            ImGui::Dummy(ImVec2(0, ui::space::XS));
            const uint64_t size = m_s->fromCopy ? m_s->copy.total : m_s->discSize;
            const uint64_t hdBytes = m_s->portableHdBytes;
            ui::Text(ui::TextStyle::Caption,
                Tf("It plays on {} without the launcher or the disc image, with the same saves and settings as the "
                   "installed game. "
                   "It never updates: to update it, export it again. Up to {}.",
                    SystemName(), ui::FormatSize(size + (m_s->portableHd ? hdBytes : 0)))
                    .c_str(),
                ui::color::Dim);
            ImGui::Dummy(ImVec2(0, ui::space::XS));
            ui::Text(ui::TextStyle::Caption, m_s->portableSettings.c_str(), ui::color::Dim);
            // Only once they are made: the copy holds them as they are (off, it is the smaller file).
            if (hdBytes != 0 && ui::BeginSettings("##portablehd"))
            {
                const std::string hint =
                    Tf("{} with them, {} without.", ui::FormatSize(size + hdBytes), ui::FormatSize(size));
                ui::SettingRow(Tr("Include HD textures"), hint.c_str());
                ui::Toggle("##portablehdtoggle", &m_s->portableHd);
                ui::EndSettings();
            }
            if (ui::ModalActions(&m_portableAsk, Tr("Choose a folder..."), Tr("Cancel")) == ui::ModalResult::Confirm)
            {
                m_portableAsk = false;
                if (const char* folder = LauncherEnv("SKY_LAUNCHER_TEST_PORTABLE_DIR")) // tests: no dialog
                    StartPortableExport(datadir::FromText(folder));
                else
                {
                    const std::string desktop = datadir::Text(DesktopFolder());
                    SDL_ShowOpenFolderDialog(PortableFolderChosen, nullptr, m_window, desktop.c_str(), false);
                }
            }
            ui::EndModal();
        }
    }

    // What the dialog shows: the HD textures when they are made (the switch off), and the settings the copy carries.
    void App::OpenPortableExport()
    {
        m_s->portableHd = false;
        m_s->portableHdBytes = HdTexturesOffered() && HdTexturesWanted(*m_s) && !m_s->hdJob.Running()
                                   ? PortableHdTexturesBytes(HdTexturesFolder())
                                   : 0;
        const portable::IniSections settings = portable::ParseIni(PortableSettings(m_config, m_s->game.id, false));
        auto get = [&](const std::string& section, const char* key) {
            auto lines = settings.find(section);
            if (lines == settings.end())
                return std::string();
            auto value = lines->second.find(key);
            return value != lines->second.end() ? value->second : std::string();
        };
        const std::string scale = get(GameVideoSection(m_s->game.id), "resolution_scale");
        LauncherConfig language;
        if (std::string code = get(m_s->game.id, "language"); !code.empty())
            language[m_s->game.id]["language"] = code;
        m_s->portableSettings = Tf("Settings: {} FPS · Resolution scale {} · {}",
            get(GameVideoSection(m_s->game.id), "fps_limit"),
            scale.empty() || scale == "auto" ? Tr("Auto") : "x" + scale, LauncherLanguageLabel(language, m_s->game));
    }

    void App::StartPortableExport(const std::filesystem::path& folder)
    {
        if (m_s->installed.runtime.empty() || m_s->disc != DiscState::Ready || Installing() ||
            m_s->portableJob.Running())
            return;
        PortableSpec spec;
        spec.gameId = m_s->game.id;
        spec.runtime = datadir::FromText(m_s->installed.runtime);
        spec.source = m_s->discPath;
        spec.collection = datadir::FromText(m_s->collection->Folder());
        spec.keys = PortableKeys(m_config, m_s->game.id);
        const bool hd = m_s->portableHd && m_s->portableHdBytes != 0;
        spec.settings = PortableSettings(m_config, m_s->game.id, hd);
        if (hd)
            spec.hdTextures = HdTexturesFolder();
        spec.info = { m_s->game.id, m_s->installed.gameVersion, m_s->installed.kitVersion, m_s->check.xexSha256,
            PortableStamp() };
        spec.folder = folder;
        spec.name = PortableName(m_s->game.id);
        if (const char* free =
                LauncherEnv("SKY_LAUNCHER_COPY_FREE_BYTES")) // tests: the room to assume, as for the copy
            spec.freeBytes = std::strtoll(free, nullptr, 10);
        m_s->portablePath.clear();
        m_s->portableMessage.clear();
        m_s->portableJob.Start(spec);
    }

    void App::FinishPortableExport()
    {
        PortableResult result = m_s->portableJob.Collect();
        if (result.outcome == PortableResult::Outcome::Done)
        {
            m_s->portablePath = result.path;
            Toast(Tf("Portable copy saved: {}", datadir::Text(result.path.filename())), ui::ToastKind::Success);
        }
        else if (result.outcome == PortableResult::Outcome::Cancelled)
            Toast(Tr("Export stopped"));
        else
        {
            m_s->portableMessage = PortableFailureText(result);
            Toast(m_s->portableMessage, ui::ToastKind::Error);
        }
    }

    // A shortcut on the Desktop that opens Arkeyan Launcher on this game and starts it, with the game's icon from its disc.
    void App::CreateShortcut()
    {
        ShortcutSpec spec;
        spec.gameId = m_s->game.id;
        spec.gameName = m_s->game.name;
        spec.launcher = SelfExecutable();
        // The built game's own icon (game_icon.png in its folder: the disc's logo, made by Arkeyan-Builder), else the disc's title icon.
        spec.iconFolder = GameInstallFolder({});
        if (std::ifstream file(spec.iconFolder / "game_icon.png", std::ios::binary); file)
            spec.iconPng.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (spec.iconPng.empty())
            spec.iconPng = m_s->meta.iconPng;
        spec.folder = DesktopFolder();
        ShortcutResult result = CreateDesktopShortcut(spec);
        Toast(result.ok ? Tr("Shortcut added to the Desktop") : result.error,
            result.ok ? ui::ToastKind::Success : ui::ToastKind::Error);
    }

    void App::DrawGameManage()
    {
        if (!m_s->saveBackupsLoaded)
            RefreshSaveBackups();
        const GameFolders folders = FoldersOf(m_s->game.id);
        ui::ButtonOptions folder;
        folder.icon = ui::Icon::Folder;
        const char* const files = Tr("Game files");
        const char* const figures = Tr(strings::OpenFigures);
        const float foldersWidth = ui::ButtonWidth(files, folder) + ui::space::S + ui::ButtonWidth(figures, folder);
        const float column = std::max(float(ui::height::SettingControl), foldersWidth);

        // The rows share their edges: a disclosure's title and the hairlines start where a settings row's label does.
        bool first = true;
        auto hairline = [&] {
            if (std::exchange(first, false))
                return;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, at.y + 0.5f),
                ImVec2(at.x + ImGui::GetContentRegionAvail().x, at.y + 0.5f),
                ui::Col(ui::color::Hairline, 110 / 255.0f), ui::stroke::Hairline);
        };
        auto settings = [&](const char* id, auto&& draw) {
            hairline();
            if (ui::BeginSettings(id, column))
            {
                draw();
                ui::EndSettings();
            }
        };
        // One unfolded at a time; a link's section is scrolled into view.
        auto disclosure = [&](const char* id, const char* title, GameSection section, ui::DisclosureOptions options,
                              auto&& draw) {
            hairline();
            options.divider = false;
            options.reveal = m_gameManage == section && m_gameSectionScroll > 0;
            if (options.reveal)
                m_gameSectionScroll--;
            const bool wasOpen = m_gameManage == section;
            bool open = wasOpen;
            const bool shown = ui::BeginDisclosure(id, title, &open, options);
            if (open != wasOpen)
            {
                m_gameManage = open ? section : GameSection::None;
                m_gameSectionScroll = 0;
            }
            if (shown)
            {
                draw();
                ui::EndDisclosure();
            }
        };

        // The games whose Nightmare the game locks until the story is beaten: offered at once, the save is not changed
        // ([<game>] nightmare_from_start).
        if (NightmareFromStartOffered(m_s->game.id))
            settings("##manage", [&] {
                ui::SettingRow(Tr("Nightmare from the start"), Tr("Before you beat the game. Your save is untouched."),
                    Tr("Difficulty"));
                bool value = ParseBool(SettingGet(m_config, m_s->game.id, "nightmare_from_start", "0"));
                if (ui::Toggle("##nightmarefromstart", &value))
                    Set(m_s->game.id, "nightmare_from_start", value ? "1" : "0");
            });

        {
            std::string state;
            switch (m_s->disc)
            {
            case DiscState::None: state = Tr("None chosen yet."); break;
            case DiscState::Verifying: state = m_s->verify.Stage() + "..."; break;
            case DiscState::Failed: state = SlotStatus(m_s).text; break;
            case DiscState::Ready:
                state = DiscDamaged(*m_s) ? std::string(Tr("Damaged copy"))
                        : m_s->fromCopy   ? std::string(Tr("No disc image needed."))
                                          : DiscFileName(m_s->discPath);
                break;
            }
            ui::DisclosureOptions disc;
            disc.summary = state.c_str();
            bool change = false;
            if (!m_s->fromCopy) // the image is not used then
            {
                disc.action = m_s->discPath.empty() ? Tr("Choose...") : Tr("Change...");
                disc.actionPressed = &change;
                // Primary: the disc to choose (again).
                disc.actionOptions.kind =
                    m_s->discPath.empty() || DiscDamaged(*m_s) ? ui::ButtonKind::Primary : ui::ButtonKind::Secondary;
                disc.actionOptions.disabled =
                    Installing() || m_s->disc == DiscState::Verifying || m_s->copyJob.Running();
            }
            disclosure("##disc", Tr("Disc"), GameSection::Disc, disc, [&] { DrawGameDisc(); });
            if (change)
                Browse();
        }

        {
            const size_t count = m_s->saveBackups.size();
            const std::string backups = count == 0   ? std::string(Tr("No backups yet."))
                                        : count == 1 ? Tf("{} backup", count)
                                                     : Tf("{} backups", count);
            ui::DisclosureOptions saves;
            saves.summary = backups.c_str();
            disclosure("##saves", Tr("Saves"), GameSection::Saves, saves, [&] { DrawSaveBackups(); });
        }

        settings("##manageactions", [&] {
            const bool disc = m_s->disc == DiscState::Ready;
            ui::SettingRow(Tr("Desktop shortcut"), disc ? nullptr : Tr("Choose the disc image first."));
            ui::ButtonOptions shortcut;
            shortcut.disabled = !disc;
            if (ui::Button(Tr("Create"), shortcut))
                CreateShortcut();

            DrawPortableExport();

            ui::SettingRow(Tr("Folders"));
            ui::AlignRight(foldersWidth);
            if (ui::Button(files, folder))
            {
                std::error_code error;
                std::filesystem::create_directories(folders.game, error);
                if (!OpenFolder(folders.game))
                    Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
            }
            ImGui::SameLine(0, ui::space::S);
            if (ui::Button(figures, folder))
            {
                SDL_CreateDirectory(m_s->collection->Folder().c_str());
                std::string path = m_s->collection->Folder();
                OpenFolder(std::filesystem::path(std::u8string(path.begin(), path.end())));
            }
        });
        disclosure("##repair", Tr("Repair or uninstall"), GameSection::Repair, {}, [&] { DrawGameRepair(); });
        DrawPortableDialog();
    }

    void App::DrawGameRepair()
    {
        if (!m_s->dataSizesLoaded)
            RefreshDataSizes();
        const GameFolders folders = FoldersOf(m_s->game.id);
        const bool busy = Installing() || m_s->hdJob.Running() || m_s->portableJob.Running(),
                   running = GameIsRunning(folders) || m_session.Running() == m_s->game.id;
        // Red only where saves or the game are lost.
        auto hold = [](ui::ButtonKind kind, const char* tooltip, bool disabled) {
            ui::ButtonOptions options;
            options.kind = kind;
            options.hold = true;
            options.tooltip = tooltip;
            options.disabled = disabled;
            return options;
        };

        // Settings rows, as the rest of the tab: what it is and what it frees on the left, one column of buttons on the right.
        const bool built = !m_s->installed.runtime.empty();
        const uint64_t caches = m_s->dataSizes.caches, saves = m_s->dataSizes.saves;
        auto freeable = [&](uint64_t bytes) {
            return running     ? std::string(Tr("The game is running: quit it first."))
                   : bytes > 0 ? Tf("Frees {}.", Freed(bytes))
                               : std::string();
        };
        const std::string cachesHint = freeable(caches), savesHint = freeable(saves),
                          gameHint = Tf("Frees {}. Saves and settings are kept.", Freed(m_s->dataSizes.installed));
        float width = 0;
        for (const char* label :
            { Tr(strings::Reinstall), Tr("Clear caches"), Tr(strings::DeleteData), Tr(strings::Uninstall) })
            width = std::max(width, ui::ButtonWidth(label));
        if (!ui::BeginSettings("##repair", std::max(width, float(ui::height::SettingControl))))
            return;
        auto action = [&](const char* label, ui::ButtonKind kind, const char* tooltip, bool disabled) {
            ui::ButtonOptions options = hold(kind, tooltip, disabled);
            options.width = width;
            ui::AlignRight(width);
            return ui::Button(label, options);
        };
        ui::SettingRow(Tr("Game"), built ? Tr("Built for this disc.") : Tr("Not built yet."));
        if (action(Tr(strings::Reinstall), ui::ButtonKind::Secondary, Tr(strings::ReinstallHint),
                busy || running || !built) &&
            !Installing())
            StartInstall();
        ui::SettingRow(Tr("Caches"), cachesHint.empty() ? nullptr : cachesHint.c_str());
        if (action(Tr("Clear caches"), ui::ButtonKind::Secondary, Tr(strings::ClearCachesHint),
                busy || running || !built || caches == 0))
            RunDataAction(ClearGameCaches(folders), N_("Caches cleared"));
        ui::SettingRow(Tr("Saves and data"), savesHint.empty() ? nullptr : savesHint.c_str());
        if (action(Tr(strings::DeleteData), ui::ButtonKind::Danger, Tr(strings::DeleteDataHint),
                busy || running || !built || saves == 0))
            RunDataAction(DeleteGameData(folders), N_("Saves and data deleted"));
        ui::SettingRow(Tr("Uninstall"), gameHint.c_str());
        if (action(
                Tr(strings::Uninstall), ui::ButtonKind::Danger, Tr(strings::UninstallHint), busy || running || !built))
            RunUninstall(UninstallGame(folders, m_config));
        ui::EndSettings();
    }
}
