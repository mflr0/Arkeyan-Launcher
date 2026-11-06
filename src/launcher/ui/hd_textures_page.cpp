// A game's HD textures (docs/tools/hd_textures.md): the switch in its settings > Video, the kit's texture step run alone when
// it is flipped on a built game, and the progress and failure it shows. Off by default ([<game>] hd_textures).
#include "launcher/env.h"
#include "launcher/ui/launcher_app.h"
#include "shared/collection/datadir.h"
#include <algorithm>
#include <fmt/format.h>
#include <fstream>

namespace launcher_ui
{
    namespace
    {
        // The pack of a whole disc, measured with the kit's builder (docs/tools/hd_textures.md).
        uint64_t PackBytes(std::string_view game)
        {
            const double gigabytes = game == "swapforce" ? 2.2 : game == "superchargers" ? 2.6 : 3.0; // Imaginators 3.0; Trap Team the same until measured
            return uint64_t(gigabytes * double(1ull << 30));
        }
    }

    // The Alchemy games: the builder reads their textures from the disc's archives. Spyro's Adventure and Giants (Lux) are not
    // read yet: their pack would hold only the textures the player dumped.
    bool App::HdTexturesOffered() const
    {
        const std::string_view id = m_s->game.id;
        return id == "swapforce" || id == "trapteam" || id == "superchargers" || id == "imaginators";
    }

    bool App::HdTexturesWanted(const GameSlot& slot) const
    {
        return ParseBool(SettingGet(m_config, slot.game.id, "hd_textures", "0"));
    }

    bool App::AnyHdTextures() const
    {
        return std::any_of(m_slots.begin(), m_slots.end(), [](const auto& slot) { return slot->hdJob.Running(); });
    }

    // The switch's row, in the Video tab's list.
    void App::DrawHdTextures()
    {
        if (!HdTexturesOffered())
            return;
        const bool built = !m_s->installed.runtime.empty();
        std::string hint = Tf("Doubles the game's textures from your disc. About {} of disk, a few minutes.", ui::FormatSize(PackBytes(m_s->game.id)));
        bool disabled = false;
        if (m_s->kit && !m_s->kit->hdTextures)
        {
            hint = Tr("Needs newer game tools: reinstall the game to get them.");
            disabled = true;
        }
        else if (m_s->hdJob.Running() || Installing())
            disabled = true;
        else if (built && (GameIsRunning(FoldersOf(m_s->game.id)) || m_session.Running() == m_s->game.id))
        {
            hint = Tr("Quit the game first.");
            disabled = true;
        }
        else if (built && m_s->disc != DiscState::Ready)
        {
            hint = Tr("Choose the disc image first.");
            disabled = true;
        }
        else if (!built)
            hint += "\n" + std::string(Tr("It is made when the game is installed."));
        ui::SettingRow(Tr("HD textures"), hint.c_str());
        bool value = HdTexturesWanted(*m_s);
        if (ui::Toggle("##hdtextures", &value, disabled))
            SetHdTextures(value);
    }

    // Under the list: the work in progress with its Stop, or why the last try did not make it.
    void App::DrawHdStatus()
    {
        if (!HdTexturesOffered())
            return;
        auto note = [](const std::string& text, unsigned color = ui::color::Dim) { ui::Text(ui::TextStyle::Caption, text.c_str(), color, ImGui::GetContentRegionAvail().x); };
        if (m_s->hdJob.Running())
        {
            const InstallProgress progress = m_s->hdJob.Snapshot();
            note(m_s->hdOn ? Tr("Making the HD textures. You can keep using the launcher; the game waits.") : Tr("Removing the HD textures."));
            if (progress.total > 0)
                ui::ProgressBar(float(double(std::min(progress.done, progress.total)) / double(progress.total)), -1, true);
            else
                ui::IndeterminateBar();
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            if (ui::Button(Tr("Stop"), { .icon = ui::Icon::Stop }))
                m_s->hdJob.Cancel();
            return;
        }
        if (!m_s->hdMessage.empty())
            note(m_s->hdMessage, ui::color::Danger);
    }

    // The player flipped the switch: the setting is kept; a built game with its disc at hand does the work now.
    void App::SetHdTextures(bool on)
    {
        Set(m_s->game.id, "hd_textures", on ? "1" : "0");
        m_s->hdMessage.clear();
        if (!m_s->installed.runtime.empty() && m_s->disc == DiscState::Ready)
            StartHdTextures(on);
    }

    void App::StartHdTextures(bool on)
    {
        m_s->kit = FindInstallKit(m_config, m_s->game.id);
        if (m_s->hdJob.StartHdTextures(m_s->kit, m_s->discPath, GameInstallFolder(m_s->check.xexSha256), on))
        {
            m_s->hdWatch = true;
            m_s->hdOn = on;
            return;
        }
        m_s->hdMessage = m_s->hdJob.Snapshot().error;
        if (on)
            Set(m_s->game.id, "hd_textures", "0");
        Toast(m_s->hdMessage, ui::ToastKind::Error);
    }

    // The work ended (polled for every game): say how, and keep the switch true to what is on disk.
    void App::FinishHdTextures()
    {
        if (!m_s->hdWatch || m_s->hdJob.Running())
            return;
        m_s->hdWatch = false;
        const InstallProgress progress = m_s->hdJob.Snapshot();
        using State = InstallProgress::State;
        if (progress.state == State::Done)
        {
            Toast(m_s->hdOn ? Tr("HD textures ready") : Tr("HD textures removed"), ui::ToastKind::Success);
            return;
        }
        // A pack left half made would be read by the game: it goes, and the switch with it.
        if (m_s->hdOn)
        {
            Set(m_s->game.id, "hd_textures", "0");
            std::error_code error;
            std::filesystem::remove_all(GameInstallFolder(m_s->check.xexSha256) / "textures-hd", error);
        }
        if (progress.state == State::Cancelled)
        {
            Toast(Tr("HD textures stopped"));
            return;
        }
        m_s->hdMessage = progress.error.empty() ? std::string(Tr("The HD textures weren't made.")) : progress.error;
        Toast(m_s->hdMessage, ui::ToastKind::Error);
    }

    // SKY_LAUNCHER_TEST_HDTEX=<game> (scratch data, SKY_LAUNCHER_TEST_DROP the game's disc, SKY_LAUNCHER_KIT a kit that knows the
    // option; SKY_LAUNCHER_TEST_HDTEX_CANCEL=1 stops the work half way): the game recorded as built, its Video tab, the switch
    // flipped as a click would, the work in progress, and how it ended, one shot each. Checks go to stderr as "FAIL ...".
    void App::TestHdTexturesScript(uint64_t now)
    {
        static int stage = 0;
        const std::string id = LauncherEnv("SKY_LAUNCHER_TEST_HDTEX");
        const bool cancel = LauncherEnv("SKY_LAUNCHER_TEST_HDTEX_CANCEL") != nullptr;
        auto shoot = [&](const char* name, uint64_t settle = 600) {
            m_testShot = fmt::format("hdtex-{}-{}", id, name);
            m_testAt = now + settle;
            stage++;
        };
        auto expect = [&](bool ok, const char* what) {
            fprintf(stderr, "%s hdtex: %s\n", ok ? "ok" : "FAIL", what);
            m_testFailures += ok ? 0 : 1;
        };
        const std::filesystem::path game = GameInstallFolder(m_s->check.xexSha256);
        switch (stage)
        {
        case 0:
            SelectGame(*FindGame(id.c_str()));
            m_page = Page::Play;
            if (m_s->disc == DiscState::None)
                StartVerify(LauncherEnv("SKY_LAUNCHER_TEST_DROP"));
            stage++;
            return;
        case 1: // the disc is checked: the game is recorded as built, with a stand-in executable
        {
            if (m_s->disc == DiscState::Verifying)
                return;
            expect(m_s->disc == DiscState::Ready, "the disc is verified");
            std::error_code error;
            std::filesystem::create_directories(game, error);
            const std::filesystem::path runtime = game / "stand-in-game";
            std::ofstream(runtime) << "stand-in\n";
            RecordInstalledGame(m_config, m_s->check.xexSha256, datadir::Text(runtime), "0.9.1", "0.1.0");
            RefreshInstall();
            m_page = Page::GameSettings;
            m_gameTab = GameTab::Video;
            expect(!HdTexturesWanted(*m_s), "off by default");
            shoot("1-off");
            return;
        }
        case 2:
            if (m_s->kit && !m_s->kit->hdTextures) // too old a kit: the switch is disabled with its reason, nothing to run
            {
                expect(!m_s->hdJob.Running() && !HdTexturesWanted(*m_s), "a kit too old for it: nothing starts");
                m_running = false;
                return;
            }
            if (cancel) // what a run stopped half way leaves behind (the fake builder writes none)
            {
                std::error_code error;
                std::filesystem::create_directories(game / "textures-hd", error);
                std::ofstream(game / "textures-hd" / "0000000000000000.dds") << "half";
            }
            SetHdTextures(true);
            expect(m_s->hdJob.Running() && HdTexturesWanted(*m_s), "flipped on: the work runs, the setting is kept");
            stage++;
            return;
        case 3: // some progress to show
        {
            const InstallProgress progress = m_s->hdJob.Snapshot();
            if (m_s->hdJob.Running() && progress.total == 0)
                return;
            shoot("2-running");
            return;
        }
        case 4: // stopped on purpose, or left to end by itself
            if (cancel)
                m_s->hdJob.Cancel();
            if (m_s->hdJob.Running())
                return;
            stage++;
            return;
        case 5:
            if (m_s->hdWatch)
                return;
            if (cancel)
            {
                expect(!HdTexturesWanted(*m_s), "stopped: the switch is back to Off");
                expect(!std::filesystem::exists(game / "textures-hd"), "stopped: no half-made folder");
                shoot("3-stopped");
            }
            else if (!m_s->hdMessage.empty())
            {
                expect(!HdTexturesWanted(*m_s), "failed: the switch is back to Off");
                shoot("3-failed");
            }
            else
            {
                expect(HdTexturesWanted(*m_s), "done: the switch stays On");
                shoot("3-done");
            }
            return;
        default:
            m_running = false;
            return;
        }
    }
}
