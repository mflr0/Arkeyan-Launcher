// The Games folder in Settings > Launcher (docs/install.md, "Games folder"): where the built games, their HD textures and
// their copied game data are, for every game. Choosing another folder moves them there, with a bar and Stop; nothing is
// rebuilt. Hidden in portable mode. A folder that is missing at Play or Install (an unplugged drive) is asked about, never
// replaced silently.
#include "launcher/ui/launcher_app.h"
#include "shared/collection/datadir.h"
#include <algorithm>
#include <mutex>

namespace launcher_ui
{
    namespace
    {
        // The folder dialog answers on a thread of its own: the folder waits here for the window's next frame.
        std::mutex g_gamesMutex;
        std::string g_gamesFolder;

        void SDLCALL GamesFolderChosen(void*, const char* const* folders, int)
        {
            std::lock_guard lock(g_gamesMutex);
            if (folders != nullptr && folders[0] != nullptr)
                g_gamesFolder = folders[0];
        }

        bool Portable() { return !datadir::PortableRoot(datadir::ProgramFolder()).empty(); }
    }

    bool App::GamesBusy() const
    {
        if (AnyInstalling() || AnyVerifying() || AnyHdTextures() || !m_session.Running().empty())
            return true;
        for (const GameInfo& game : g_games)
            if (GameIsRunning(FoldersOf(game.id)))
                return true;
        return std::any_of(
            m_slots.begin(), m_slots.end(), [](const auto& slot) { return slot->portableJob.Running(); });
    }

    void App::DrawGamesFolder()
    {
        if (Portable())
            return;
        {
            std::lock_guard lock(g_gamesMutex);
            if (!g_gamesFolder.empty())
                ChooseGamesFolder(datadir::FromText(std::exchange(g_gamesFolder, std::string())));
        }
        const bool moving = m_gamesMove.Running(), busy = GamesBusy();
        const std::filesystem::path current = ChosenGamesFolder(m_config);
        ui::SectionHeader(Tr("Games folder"), busy && !moving
                                                  ? Tr("Wait for the game to finish.")
                                                  : Tr("Where every game is built: another drive saves room."));
        auto note = [](const std::string& text) { ui::Text(ui::TextStyle::Caption, text.c_str(), ui::color::Dim); };
        if (moving)
        {
            const uint64_t total = m_gamesMove.Total();
            note(Tf("Moving the games: {} of {}", ui::FormatSize(m_gamesMove.Done()), ui::FormatSize(total)));
            ui::ProgressBar(total == 0 ? 0.0f : float(double(m_gamesMove.Done()) / double(total)), -1, true);
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            if (ui::Button(Tr("Stop"), { .icon = ui::Icon::Stop }))
                m_gamesMove.Stop();
            return;
        }
        // Where they are now: the folder's name, its path in the tooltip (no path in the text).
        const std::string name = current.empty()
                                     ? std::string(Tr("Data folder"))
                                     : datadir::Text(current.filename().empty() ? current : current.filename());
        note(Tf("Now: {}", name));
        const std::string path = datadir::Text(current.empty() ? LauncherDataFolder() : current);
        ui::ButtonOptions choose;
        choose.disabled = busy;
        if (ui::Button(Tr("Choose..."), choose))
        {
            const std::string start = datadir::Text(current.empty() ? LauncherDataFolder() : current);
            SDL_ShowOpenFolderDialog(GamesFolderChosen, nullptr, m_window, start.c_str(), false);
        }
        ImGui::SameLine(0, ui::space::S);
        ui::ButtonOptions folder;
        folder.icon = ui::Icon::Folder;
        folder.tooltip = path.c_str();
        if (ui::Button(Tr("Show in folder"), folder))
        {
            std::error_code error;
            if (current.empty())
                std::filesystem::create_directories(LauncherDataFolder(), error);
            if (!OpenFolder(current.empty() ? LauncherDataFolder() : current))
                Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
        }
        if (!current.empty())
        {
            ImGui::SameLine(0, ui::space::S);
            ui::ButtonOptions reset;
            reset.disabled = busy;
            if (ui::Button(Tr("Use default"), reset))
                ChooseGamesFolder({});
        }
    }

    // A folder the player chose (empty: the data folder): checked, the room counted, then the games move.
    void App::ChooseGamesFolder(const std::filesystem::path& chosen)
    {
        if (m_gamesMove.Running())
        {
            Toast(Tr("Wait for the games to move"), ui::ToastKind::Error);
            return;
        }
        if (GamesBusy())
        {
            Toast(Tr("Quit the game to move the games"), ui::ToastKind::Error);
            return;
        }
        std::filesystem::path target = chosen;
        std::error_code error;
        if (!target.empty() && std::filesystem::equivalent(target, LauncherDataFolder(), error))
            target.clear(); // the data folder itself: the default
        if (const gamesfolder::Refusal refusal = gamesfolder::Check(target); refusal != gamesfolder::Refusal::None)
        {
            Toast(gamesfolder::RefusalText(refusal), ui::ToastKind::Error);
            return;
        }
        const std::filesystem::path from = ChosenGamesFolder(m_config);
        if (!from.empty() && !target.empty() && std::filesystem::equivalent(from, target, error))
            return; // already there
        // The old folder is gone (an unplugged drive): nothing to move, the setting alone changes.
        if (!from.empty() && !std::filesystem::is_directory(from, error))
        {
            gamesfolder::Plan none{ from, target, {}, 0 };
            gamesfolder::Record(m_config, none);
            SaveConfig();
            ApplyGamesFolder(m_config);
            RefreshInstall();
            return;
        }
        gamesfolder::Plan plan = gamesfolder::MakePlan(m_config, from, target);
        if (!m_gamesMove.Start(std::move(plan)))
            Toast(Tr("Couldn't move the games"), ui::ToastKind::Error);
    }

    // The move ended: the settings follow it, and every game looks for its build again.
    void App::PollGamesMove()
    {
        gamesfolder::Plan plan;
        gamesfolder::Result result;
        if (!m_gamesMove.TakeResult(plan, result))
            return;
        using State = gamesfolder::Result::State;
        if (result.state == State::Done)
        {
            gamesfolder::Record(m_config, plan);
            if (!SaveConfig())
                Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            else
                Toast(Tr("Games moved"), ui::ToastKind::Success);
            ApplyGamesFolder(m_config);
        }
        else if (result.state == State::Stopped)
            Toast(Tr("Move stopped: the games stay where they were"));
        else if (result.state == State::NoRoom)
            Toast(Tf("Not enough free space: {} needed", ui::FormatSize(result.needed)), ui::ToastKind::Error);
        else
            Toast(Tr("Couldn't move the games: they stay where they were"), ui::ToastKind::Error);
        GameSlot* shown = m_s;
        for (const auto& slot : m_slots)
        {
            m_s = slot.get();
            SetActiveGame(slot->game.id);
            RefreshInstall();
        }
        m_s = shown;
        SetActiveGame(m_s->game.id);
    }

    bool App::GamesFolderMissingFirst(std::function<void()> retry)
    {
        if (m_gamesMissingOpen)
            return true;
        if (m_gamesMove.Running())
        {
            Toast(Tr("Wait for the games to move"), ui::ToastKind::Error);
            return true;
        }
        if (!GamesFolderMissing())
            return false;
        m_gamesMissingOpen = true;
        m_gamesMissingThen = std::move(retry);
        return true;
    }

    void App::DrawGamesFolderMissing()
    {
        if (!m_gamesMissingOpen)
            return;
        bool open = true, retry = false, useDefault = false;
        if (ui::BeginModal("##gamesmissing", Tr("Games folder"), &open, ui::ModalSize::Medium))
        {
            ui::Text(ui::TextStyle::Body, GamesFolderMissingMessage(), ui::color::Ink);
            ImGui::Dummy(ImVec2(0, ui::space::S));
            const ui::ModalResult answer = ui::ModalActions(&open, Tr("Retry"), Tr("Use default"));
            retry = answer == ui::ModalResult::Confirm;
            useDefault = answer == ui::ModalResult::Cancel;
            ui::EndModal(Tr("Close")); // B closes it and changes nothing
        }
        if (open)
            return;
        m_gamesMissingOpen = false;
        std::function<void()> then = std::move(m_gamesMissingThen);
        m_gamesMissingThen = nullptr;
        if (useDefault)
        {
            // Back to the data folder: the games on the missing drive stay there, untouched.
            gamesfolder::Plan none{ ChosenGamesFolder(m_config), {}, {}, 0 };
            gamesfolder::Record(m_config, none);
            if (!SaveConfig())
                Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
            ApplyGamesFolder(m_config);
            RefreshInstall();
            return;
        }
        if (retry && then)
            then(); // still missing: asked again
    }
}
