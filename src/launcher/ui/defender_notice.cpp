// The Windows Defender notice: shown once, before the first install, game update or launcher update on Windows. Defender's
// machine-learning guess (Trojan:Win32/Bearfoos.A!ml) flags the builder, the launcher or a built game; the notice says why
// and how to let them run (README, "Windows Defender alerts").
#include "launcher/ui/launcher_app.h"
#include "launcher/env.h"
#include "launcher/ui/launcher_ui.h"
#include <SDL3/SDL.h>

namespace launcher_ui
{
    namespace
    {
        constexpr const char* AlertsUrl = "https://github.com/mflr0/Arkeyan-Launcher#windows-defender-alerts";
    }

    bool App::DefenderNoticeFirst(std::function<void()> then)
    {
        // Tests: SKY_LAUNCHER_TEST_DEFENDER shows it on any system; ArkeyanLauncherTest sets SKY_LAUNCHER_NO_DEFENDER_NOTICE
        // so its scripted installs and updates on Windows are not held.
#if defined(_WIN32)
        bool windows = LauncherEnv("SKY_LAUNCHER_NO_DEFENDER_NOTICE") == nullptr;
#else
        bool windows = false;
#endif
        windows = windows || LauncherEnv("SKY_LAUNCHER_TEST_DEFENDER") != nullptr;
        if (m_defenderNoticeOpen)
            return true; // already asking: the first request waits for the answer
        if (!windows || SettingGet(m_config, "launcher", "defender_notice", "") == "1")
            return false;
        m_defenderNoticeOpen = true;
        m_defenderThen = std::move(then);
        return true;
    }

    void App::DrawDefenderNotice()
    {
        if (!m_defenderNoticeOpen)
            return;
        bool open = true, go = false;
        if (ui::BeginModal("##defender", Tr("Windows Defender may block the install"), &open, ui::ModalSize::Large))
        {
            ui::Text(ui::TextStyle::Body,
                Tr("Windows Defender may report Arkeyan-Builder.exe, ArkeyanLauncher.exe or a game built here as "
                   "Trojan:Win32/Bearfoos.A!ml. "
                   "The !ml ending means no known malware was found: Defender's machine-learning model guesses from "
                   "what the program does. "
                   "The launcher is not signed, downloads a compiler, builds a new game on your PC and updates itself: "
                   "that is the whole guess."),
                ui::color::Ink);
            ImGui::Dummy(ImVec2(0, ui::space::XS));
            ui::Text(ui::TextStyle::Body,
                Tr("The author has submitted the launcher and its source code to Microsoft as a false positive; the "
                   "review is pending."),
                ui::color::InkSoft);
            ImGui::Dummy(ImVec2(0, ui::space::S));
            ui::Text(ui::TextStyle::Body, Tr("If Windows Defender blocks a file:"), ui::color::Ink);
            const char* steps[] = {
                N_("1. Open Windows Security, then Virus & threat protection > Protection history."),
                N_("2. Open the blocked item, then Actions > Allow on device."),
                N_("3. In Virus & threat protection settings, choose Manage settings > Exclusions > Add or remove "
                   "exclusions > Add an exclusion > Folder, and pick %APPDATA%\\Skylanders\\kit."),
                N_("4. Back here, choose Retry."),
            };
            for (const char* step : steps)
                ui::Text(ui::TextStyle::Body, Tr(step), ui::color::InkSoft);
            ImGui::Dummy(ImVec2(0, ui::space::XS));
            ui::Text(ui::TextStyle::Caption,
                Tr("Every download is checked against the release's SHA-256 before it runs, and every release is built "
                   "by GitHub Actions from the public source."),
                ui::color::Dim);
            go = ui::ModalActions(&open, Tr("Continue"), Tr("Not now")) == ui::ModalResult::Confirm;
            // Why, at the left of the actions' row.
            const float actionsY = ImGui::GetItemRectMin().y, afterY = ImGui::GetCursorPosY();
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetCursorStartPos().x, actionsY));
            if (ui::Button(Tr("Why this happens"), { .kind = ui::ButtonKind::Ghost, .icon = ui::Icon::External }))
                SDL_OpenURL(AlertsUrl);
            ImGui::SetCursorPosY(afterY);
            ImGui::Dummy(ImVec2(0, 0));
            ui::EndModal();
        }
        if (open)
            return;
        // Shown once, whichever way it closed; Continue goes on with what the player asked for.
        m_defenderNoticeOpen = false;
        Set("launcher", "defender_notice", "1");
        std::function<void()> then = std::move(m_defenderThen);
        m_defenderThen = nullptr;
        if (go && then)
            then();
    }
}
