// A game's own settings (the Settings button on its Play page), what a player looks for first on the left: Video (display, image
// quality, textures), Keyboard, Controller, Sound, Language, then the game's files: Disc, Saves and Game (shortcut, folders,
// reinstall, delete data, uninstall). Arkeyan Launcher's own settings and what all games share (the controller, the global
// keys) are in the gear's Settings (launcher_ui.cpp, controls_page.cpp). The video keys are the game's own copy in
// [<game>.video] (game_settings.h); its Keyboard and Controller tabs are in controls_page.cpp (game_keys.h).
#include "launcher/ui/launcher_app.h"
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

        static const char* const Tabs[] = { N_("Video"), N_("Keyboard"), N_("Controller"), N_("Sound"), N_("Language"),
            N_("Disc"), N_("Saves"), N_("Game") };
        int tab = int(m_gameTab);
        DrawTabBar(Tabs, int(std::size(Tabs)), &tab);
        m_gameTab = GameTab(tab);
        BeginCard("##gamesettings", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        switch (m_gameTab)
        {
        case GameTab::Video: DrawGameVideo(); break;
        case GameTab::Keyboard: DrawGameKeyboard(); break;
        case GameTab::Controller: DrawGameController(); break;
        case GameTab::Sound: DrawGameSound(); break;
        case GameTab::Language: DrawGameLanguage(); break;
        case GameTab::Disc: DrawGameDisc(); break;
        case GameTab::Saves: DrawSaveBackups(); break;
        case GameTab::Game: DrawGameTab(); break;
        }
        EndCard();
        if (m_testHdTex)
            ImGui::SetScrollHereY(1.0f); // the texture pack screenshots show the bottom of the Video tab
        EndSettingsPage();
    }

    // ---- Video, sound and language --------------------------------------------------------------------------------------

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

        // The game's HD textures (made from the disc). The player's own packs, their folder and the dump that makes them are
        // greyed and off until custom packs come (0.9.4): PLAY passes texture_packs=0 and texture_dump=0 (ApplyGameVideo).
        ui::SectionHeader(Tr("Textures"));
        if (ui::BeginSettings("##textures"))
        {
            DrawHdTextures();
            auto later = [&](const char* label, const char* hint, const char* id) {
                const std::string text = std::string(Tr(hint)) + "\n" + Tr("Coming later");
                ui::SettingRow(Tr(label), text.c_str());
                bool off = false;
                ui::Toggle(id, &off, true);
            };
            later(N_("Custom texture packs"), N_("Replaces the game's textures with your pack."), "##texturepacks");
            ui::SettingRow(Tr("Custom packs folder"), nullptr);
            ui::ButtonOptions folder;
            folder.icon = ui::Icon::Folder;
            folder.disabled = true;
            ui::Button(Tr("Show in folder"), folder);
            later(N_("Dump textures"), N_("For pack makers: saves the textures as DDS files."), "##texturedump");
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
        ui::SectionHeader(Tr("Disc image"), nullptr, false);
        auto filename = [](const std::string& path) {
            return LauncherMiddleEllipsis(datadir::Text(datadir::FromText(path).filename()), 60);
        };
        if (m_s->fromCopy)
        {
            // The game plays from its copied game data: the image is not used, so its buttons are not offered.
            const std::string iso = SettingGet(m_config, m_s->game.id, "iso");
            ui::Text(ui::TextStyle::Body, iso.empty() ? Tr("None chosen.") : filename(iso).c_str());
            ui::Text(ui::TextStyle::Caption, Tr("Not needed: the game plays from its copied data."), ui::color::Dim);
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            DrawDiscCheck(); // of the copy
            DrawCheckAgain();
        }
        else
        {
            ui::Text(
                ui::TextStyle::Body, m_s->discPath.empty() ? Tr("None chosen yet.") : filename(m_s->discPath).c_str());
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
            ui::ButtonOptions choose;
            choose.kind = (other != nullptr || !m_s->discPath.empty()) && !DiscDamaged(*m_s)
                              ? ui::ButtonKind::Secondary
                              : ui::ButtonKind::Primary; // Primary: the disc to choose (again)
            choose.disabled = busy;
            if (ui::Button(m_s->discPath.empty() ? Tr("Choose...") : Tr("Change..."), choose))
                Browse();
            ImGui::SameLine(0, ui::space::S);
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
        ui::SectionHeader(Tr("Save backups"),
            running ? Tr("The game is running: quit it to restore a backup.") : Tr("Earlier versions of your saves."),
            false);
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
        ui::SectionHeader(Tr("Portable copy"),
            !built   ? Tr("Install the game first.")
            : !ready ? Tr("Choose the disc image first.")
                     : Tr("The game in one file: it plays without the launcher or the disc image."));
        auto note = [](const std::string& text) { ui::Text(ui::TextStyle::Caption, text.c_str(), ui::color::Dim); };
        if (m_s->portableJob.Running())
        {
            const uint64_t total = m_s->portableJob.Total();
            note(total == 0
                     ? Tr("Exporting...")
                     : Tf("Exporting: {} of {}", ui::FormatSize(m_s->portableJob.Bytes()), ui::FormatSize(total)));
            ui::ProgressBar(m_s->portableJob.Fraction(), -1, true);
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            if (ui::Button(Tr("Stop"), { .icon = ui::Icon::Stop }))
                m_s->portableJob.Cancel();
            return;
        }
        ui::ButtonOptions exportOptions;
        exportOptions.icon = ui::Icon::ArrowUp; // out of the launcher, as every export
        exportOptions.disabled = !ready || Installing();
        if (ui::Button(Tf("Export for {}", SystemName()).c_str(), exportOptions))
            m_portableAsk = true;
        if (!m_s->portablePath.empty())
        {
            ImGui::SameLine(0, ui::space::S);
            ui::ButtonOptions folder;
            folder.icon = ui::Icon::Folder;
            if (ui::Button(Tr("Show in folder"), folder) && !OpenFolder(m_s->portablePath.parent_path()))
                Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
        }
        if (!m_s->portableMessage.empty())
            note(m_s->portableMessage);

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
            ui::Text(ui::TextStyle::Caption,
                Tf("It plays on {} without the launcher or the disc image, with the same saves and settings as the "
                   "installed game. "
                   "It never updates: to update it, export it again. Up to {}.",
                    SystemName(), ui::FormatSize(size))
                    .c_str(),
                ui::color::Dim);
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

    void App::DrawGameTab()
    {
        if (!m_s->dataSizesLoaded)
            RefreshDataSizes();
        const GameFolders folders = FoldersOf(m_s->game.id);
        const bool busy = Installing() || m_s->hdJob.Running() || m_s->portableJob.Running(),
                   running = GameIsRunning(folders) || m_session.Running() == m_s->game.id;
        auto hold = [](const char* tooltip, bool disabled = false) {
            ui::ButtonOptions options;
            options.kind = ui::ButtonKind::Danger;
            options.hold = true;
            options.tooltip = tooltip;
            options.disabled = disabled;
            return options;
        };

        // The games whose Nightmare the game locks until the story is beaten: offered at once, the save is not changed
        // ([<game>] nightmare_from_start).
        const bool lockedNightmare = NightmareFromStartOffered(m_s->game.id);
        if (lockedNightmare)
        {
            ui::SectionHeader(Tr("Difficulty"), nullptr, false);
            if (ui::BeginSettings("##nightmare"))
            {
                ui::SettingRow(Tr("Nightmare from the start"), Tr("Before you beat the game. Your save is untouched."));
                bool value = ParseBool(SettingGet(m_config, m_s->game.id, "nightmare_from_start", "0"));
                if (ui::Toggle("##nightmarefromstart", &value))
                    Set(m_s->game.id, "nightmare_from_start", value ? "1" : "0");
                ui::EndSettings();
            }
        }

        ui::SectionHeader(Tr("Desktop shortcut"),
            m_s->disc != DiscState::Ready ? Tr("Choose the disc image first.") : nullptr, lockedNightmare);
        ui::ButtonOptions shortcut;
        shortcut.disabled = m_s->disc != DiscState::Ready;
        if (ui::Button(Tr("Create"), shortcut))
            CreateShortcut();

        DrawPortableExport();

        ui::SectionHeader(Tr("Folders"));
        ui::ButtonOptions folder;
        folder.icon = ui::Icon::Folder;
        if (ui::Button(Tr("Game files"), folder))
        {
            std::error_code error;
            std::filesystem::create_directories(folders.game, error);
            if (!OpenFolder(folders.game))
                Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
        }
        ImGui::SameLine(0, ui::space::S);
        if (ui::Button(Tr(strings::OpenFigures), folder))
        {
            SDL_CreateDirectory(m_s->collection->Folder().c_str());
            std::string figures = m_s->collection->Folder();
            OpenFolder(std::filesystem::path(std::u8string(figures.begin(), figures.end())));
        }

        ui::SectionHeader(
            Tr("Game"), !m_s->installed.runtime.empty() ? Tr("Built for this disc.") : Tr("Not built yet."));
        if (ui::Button(Tr(strings::Reinstall),
                hold(Tr(strings::ReinstallHint), busy || running || m_s->installed.runtime.empty())) &&
            !Installing())
            StartInstall();

        const uint64_t caches = m_s->dataSizes.caches, saves = m_s->dataSizes.saves;
        const bool built = !m_s->installed.runtime.empty();
        std::string cleanHelp =
            caches != 0 && saves != 0
                ? Tf("Clearing caches frees {}; deleting saves and data frees {} more.", Freed(caches), Freed(saves))
            : caches != 0 ? Tf("Clearing caches frees {}.", Freed(caches))
            : saves != 0  ? Tf("Deleting saves and data frees {}.", Freed(saves))
                          : std::string();
        ui::SectionHeader(Tr("Clean up"), running             ? Tr("The game is running: quit it first.")
                                          : cleanHelp.empty() ? nullptr
                                                              : cleanHelp.c_str());
        if (ui::Button(
                Tr("Clear caches"), hold(Tr(strings::ClearCachesHint), busy || running || !built || caches == 0)))
            RunDataAction(ClearGameCaches(folders), N_("Caches cleared"));
        ImGui::SameLine(0, ui::space::S);
        if (ui::Button(
                Tr(strings::DeleteData), hold(Tr(strings::DeleteDataHint), busy || running || !built || saves == 0)))
            RunDataAction(DeleteGameData(folders), N_("Saves and data deleted"));

        ui::SectionHeader(
            Tr("Uninstall"), Tf("Frees {}. Saves and settings are kept.", Freed(m_s->dataSizes.installed)).c_str());
        if (ui::Button(Tr(strings::Uninstall),
                hold(Tr(strings::UninstallHint), busy || running || m_s->installed.runtime.empty())))
            RunUninstall(UninstallGame(folders, m_config));
    }
}
