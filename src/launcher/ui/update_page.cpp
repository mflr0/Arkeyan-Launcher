// Updates in the window: the check at start, the banner ("Update available" and its progress), What's new, and the About
// page (version, "Check now", credits and licenses). The logic is in launcher/updater.cpp (docs/updates.md).
#include "launcher/ui/launcher_app.h"
#include "launcher/env.h"
#include "shared/collection/datadir.h"
#include "launcher/ui/launcher_ui.h"
#include "launcher/ui/text_fit.h"
#include <ctime>
#include <fmt/format.h>

std::string GameUpdateSummary(const std::vector<std::string>& games)
{
    if (games.empty())
        return Tr("Arkeyan Launcher is up to date.");
    return games.size() == 1 ? Tf("Arkeyan Launcher is up to date. An update is available for {}.", games.front())
                             : Tf("Arkeyan Launcher is up to date. {} game updates available.", games.size());
}

namespace launcher_ui
{
    namespace
    {
        constexpr float BannerH = ui::height::Large;
        constexpr char Project[] = "https://github.com/mflr0/Arkeyan-Launcher";

        int64_t Now() { return int64_t(std::time(nullptr)); }

        std::string Fit(const std::string& text, float width)
        {
            return launcher_ui::FitText(
                text, width, [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x; });
        }

        const char* PlatformName()
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

    void App::StartUpdateCheck(bool manual)
    {
        if (m_updates.Busy() || m_downgradeOpen)
            return;
        m_updateManual = manual;
        if (manual)
        {
            m_updateLater = false;
            m_updateDoneNote.clear();
        }
        UpdateManager::Settings settings;
        settings.repo = SettingGet(m_config, "update", "repo", "mflr0/Arkeyan-Launcher");
        settings.kitRepo = SettingGet(m_config, "update", "kit_repo", "mflr0/Arkeyan-Launcher");
        settings.betas = ReceiveBetas();
        m_updateWasBusy = true;
        m_updates.Check(settings);
    }

    // "Receive betas": off unless chosen, on by default in a test build (it then offers the next one, and the final version).
    bool App::ReceiveBetas() const
    {
        return ParseBool(SettingGet(m_config, "update", "betas", IsBetaVersion(LauncherVersion()) ? "1" : "0"));
    }

    // At the end of a check or an update: remember it, say the answer to a manual check, carry on with the game update.
    void App::PollUpdates()
    {
        if (m_updateWasBusy && !m_updates.Busy())
        {
            m_updateWasBusy = false;
            UpdateManager::State state = m_updates.Snapshot();
            if (state.phase == UpdateManager::Phase::Idle)
            {
                Set("update", "last_check", std::to_string(Now()));
                if (state.checked)
                    m_gameUpdates = state.games; // offline or failed: silent, the list stays as it was
                if (m_updateManual)
                    m_updateDoneNote = !state.checked ? Tf("Couldn't check: {}", state.checkError)
                                       : state.launcherNewer
                                           ? (state.downgrade ? Tf("Version {} (the final release)", state.launcher.tag)
                                                              : Tf("Version {} is available.", state.launcher.tag))
                                       : AnyGameUpdate() ? GameUpdateNote()
                                                         : Tr("Arkeyan Launcher is up to date.");
            }
        }
    }

    std::string App::GameUpdateNote() const
    {
        std::vector<std::string> games;
        for (const auto& slot : m_slots)
            if (UpdateAvailable(*slot))
                games.emplace_back(slot->game.name);
        return GameUpdateSummary(games);
    }

    float App::BannerHeight()
    {
        UpdateManager::State state = m_updates.Snapshot();
        using Phase = UpdateManager::Phase;
        bool progress = state.phase == Phase::Copying || state.phase == Phase::Downloading ||
                        state.phase == Phase::Verifying || state.phase == Phase::Installing;
        bool offer = state.launcherNewer && !m_updateLater;
        bool shown = progress || state.restartNeeded || (state.phase == Phase::Failed && !state.error.empty()) ||
                     offer || (state.phase == Phase::Done && !state.note.empty());
        return shown ? BannerH + (ui::Ctx().glyphs != ui::Glyphs::Keyboard ? ui::height::Normal : 0) : 0;
    }

    void App::DrawUpdateBanner(float height)
    {
        UpdateManager::State state = m_updates.Snapshot();
        using Phase = UpdateManager::Phase;
        ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(SidebarW, 0));
        ImGui::SetNextWindowSize(ImVec2(display.x - SidebarW, height));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Rgb(BottomBar));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui::space::L, ui::space::XS));
        ImGui::Begin("##updatebanner", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 at = ImGui::GetWindowPos();
        draw->AddRectFilled(ImVec2(at.x, at.y + height - ui::stroke::Selection),
            ImVec2(at.x + display.x - SidebarW, at.y + height), Rgb(ui::color::AccentFillPressed));

        // Buttons are laid out from the right; the text takes what is left.
        struct Button
        {
            const char* label;
            bool primary;
            int action;
        };
        std::vector<Button> buttons;
        std::string text;
        float barFraction = -1;
        bool progress = state.phase == Phase::Copying || state.phase == Phase::Downloading ||
                        state.phase == Phase::Verifying || state.phase == Phase::Installing;
        enum
        {
            None,
            WhatsNew,
            UpdateLauncher,
            Later,
            Restart,
            Cancel,
            Retry,
            Dismiss
        };
        if (state.restartNeeded)
        {
            text = Tf("Arkeyan Launcher {} is installed. Restart to use it.", state.launcher.tag);
            buttons = { { Tr("Restart now"), true, Restart } };
        }
        else if (progress)
        {
            text = state.phase == Phase::Copying       ? Tr("Copying saves and figures")
                   : state.phase == Phase::Downloading ? Tr("Downloading Arkeyan Launcher")
                   : state.phase == Phase::Verifying   ? Tr("Checking Arkeyan Launcher")
                                                       : Tr("Installing Arkeyan Launcher");
            barFraction = state.phase == Phase::Downloading ? state.progress : -1;
            if (state.phase == Phase::Downloading)
                buttons = { { Tr("Cancel"), false, Cancel } };
        }
        else if (state.phase == Phase::Failed)
        {
            text = Tf("The update failed: {}", state.error);
            buttons = { { Tr("Retry"), true, Retry }, { Tr("Close"), false, Dismiss } };
        }
        else if (state.phase == Phase::Done && !state.note.empty())
        {
            text = state.note;
            buttons = { { Tr("Close"), false, Dismiss } }; // as the failed one: one word to dismiss a banner
        }
        else if (state.launcherNewer && !m_updateLater)
        {
            text = state.downgrade ? Tf("Version {} (the final release)", state.launcher.tag)
                                   : Tf("Update available: {}", state.launcher.tag);
            buttons = { { Tr("Update now"), true, UpdateLauncher }, { Tr("What's new"), false, WhatsNew },
                { Tr("Later"), false, Later } };
        }
        // else: BannerHeight said there is nothing to show, and the banner is not drawn

        auto optionsOf = [](const Button& button) {
            ui::ButtonOptions options;
            options.kind = button.primary ? ui::ButtonKind::Primary : ui::ButtonKind::Secondary;
            options.overArt = !button.primary;
            return options;
        };
        float buttonsW = 0;
        for (const Button& button : buttons)
            buttonsW += ui::ButtonWidth(button.label, optionsOf(button)) + ui::space::S;
        const float contentW = display.x - SidebarW - 2 * ui::space::L;
        const float textW = contentW - buttonsW - ui::space::M;
        const float legendH = ui::Ctx().glyphs != ui::Glyphs::Keyboard ? ui::height::Normal : 0;
        const float middle = (height - legendH - ui::stroke::Selection) / 2;
        ImGui::SetCursorPos(ImVec2(ui::space::L, std::round(middle - ui::SizeOf(ui::TextStyle::Body) / 2)));
        std::string shown = ui::Ellipsize(text.c_str(),
            progress ? std::min(textW, ui::TextSize(ui::TextStyle::Body, text.c_str(), nullptr, true).x) : textW);
        ui::Text(ui::TextStyle::Body, shown.c_str(), ui::color::Ink, -1, true);
        if (shown != text && ImGui::IsItemHovered()) // cut to fit: the whole message under the pointer
            ImGui::SetTooltip("%s", text.c_str());
        if (progress) // the wait after the words: a bar with its percentage, or a sweep when no end is known
        {
            const float labelW = ui::TextSize(ui::TextStyle::Body, shown.c_str(), nullptr, true).x;
            const float barW = textW - labelW - ui::space::M;
            if (barW > 0)
            {
                ImGui::SameLine(0, ui::space::M);
                ImGui::SetCursorPosY(middle - ui::space::XS / 2);
                if (barFraction >= 0)
                    ui::ProgressBar(barFraction, barW, true);
                else
                    ui::IndeterminateBar(barW);
            }
        }

        float x = ui::space::L + contentW - buttonsW + ui::space::S;
        int pressed = None;
        ui::NavJoin("##page"); // the band's buttons are the page's top row for the D-pad
        for (const Button& button : buttons)
        {
            ui::ButtonOptions options = optionsOf(button);
            ImGui::SetCursorPos(ImVec2(x, middle - ui::HeightOf(ui::Size::Normal) / 2));
            ImGui::PushID(button.action);
            if (ui::Button(button.label, options))
                pressed = button.action;
            ImGui::PopID();
            x += ui::ButtonWidth(button.label, options) + ui::space::S;
        }
        ui::NavJoin(nullptr);
        const int backAction = state.restartNeeded                 ? None
                               : state.phase == Phase::Downloading ? Cancel
                               : state.phase == Phase::Failed || (state.phase == Phase::Done && !state.note.empty())
                                   ? Dismiss
                               : !progress && state.launcherNewer && !m_updateLater ? Later
                                                                                    : None;
        const char* back = backAction == Cancel    ? Tr("Cancel")
                           : backAction == Later   ? Tr("Later")
                           : backAction == Dismiss ? Tr("Close")
                                                   : Tr("Back");
        if (backAction != None)
        {
            const bool pressedBack = ui::PadAction(ui::PadButton::East, back);
            m_bannerTakesBack = ui::HasInput() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            if (m_bannerTakesBack && pressedBack)
                pressed = backAction;
        }
        if (legendH > 0)
        {
            ImGui::SetCursorPos(ImVec2(ui::space::L, height - legendH));
            ui::PageLegend(back, contentW, false, !buttons.empty());
        }
        // A banner that loses its controls must not strand the focus in a window with no stops.
        if (pressed != None || (buttons.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
        {
            RequestFocus(FocusTo::Page);
            m_bannerTakesBack = false;
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        switch (pressed)
        {
        case WhatsNew: m_whatsNewOpen = true; break;
        case UpdateLauncher: UpdateLauncherNow(); break;
        case Later: m_updateLater = true; break;
        case Cancel: m_updates.Cancel(); break;
        case Retry: UpdateLauncherNow(); break;
        case Dismiss: m_updates.Dismiss(); break;
        case Restart:
        {
            std::filesystem::path self = LauncherSelf();
            std::string program = datadir::Text(self);
            std::vector<const char*> arguments;
            // The new launcher waits for this one to close and hand it the lock (single_instance.h).
#if defined(__APPLE__)
            arguments = { "/usr/bin/open", "-n", program.c_str(), "--args", "--after-update", nullptr };
#else
            arguments = { program.c_str(), "--after-update", nullptr };
#endif
            if (SDL_Process* process = SDL_CreateProcess(arguments.data(), false))
            {
                SDL_DestroyProcess(process);
                m_running = false;
            }
            else
                Toast(Tr("Couldn't restart: start Arkeyan Launcher again"), ui::ToastKind::Error);
            break;
        }
        }
    }

    void App::UpdateLauncherNow()
    {
        if (m_updates.Busy() || !m_updates.Snapshot().launcherNewer)
            return;
        if (DefenderNoticeFirst([this] { UpdateLauncherNow(); }))
            return;
        if (m_updates.Snapshot().downgrade)
        {
            m_whatsNewOpen = false;
            m_downgradeOpen = true;
            return;
        }
        m_updates.UpdateLauncher();
        m_updateWasBusy = true;
    }

    void App::DrawDowngrade()
    {
        if (!m_downgradeOpen)
            return;
        const auto state = m_updates.Snapshot();
        bool open = true;
        if (ui::BeginModal("##downgrade", Tr("Go back to the final release?"), &open, ui::ModalSize::Medium))
        {
            ui::Text(ui::TextStyle::Body, Tf("Going back to {}. Saves and figures made with the test build may not "
                                             "open in it. A copy of them is kept.",
                                              state.launcher.tag)
                                              .c_str());
            ImGui::Dummy(ImVec2(0, ui::space::S));
            if (ui::ModalActions(&open, Tr("Continue"), Tr("Cancel")) == ui::ModalResult::Confirm)
            {
                m_updates.UpdateLauncher();
                m_updateWasBusy = true;
            }
            ui::EndModal();
        }
        if (!open)
        {
            m_downgradeOpen = false;
            RequestFocus(FocusTo::Page);
        }
    }

    // The release notes of the update on offer.
    void App::DrawWhatsNew()
    {
        if (!m_whatsNewOpen)
            return;
        UpdateManager::State state = m_updates.Snapshot();
        bool open = true;
        if (ui::BeginModal(
                "##whatsnew", Tf("What's new in {}", state.launcher.tag).c_str(), &open, ui::ModalSize::Large))
        {
            // The notes in an inset card as tall as the window allows (the title and the actions take the rest): the release's
            // Markdown read simply, its headings as bold lines, its "- " items as bullets, the rest as written.
            const float room =
                ImGui::GetIO().DisplaySize.y - 2 * ui::space::XL - 3 * ui::height::Large - ui::ModalLegendHeight();
            ui::BeginCard("##notes",
                ImVec2(
                    0, std::clamp(room, 3.0f * ui::height::Large, 8.0f * ui::height::Large - ui::ModalLegendHeight())),
                { .inset = true, .padding = ui::space::M });
            ImGui::SetScrollX(0.0f);
            std::string notes = Trimmed(StripHtmlComments(state.launcher.notes));
            if (notes.empty())
                notes = Tr("No notes were published with this version.");
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui::space::XS, ui::space::XXS));
            size_t start = 0;
            bool first = true;
            while (start <= notes.size())
            {
                size_t end = notes.find('\n', start);
                if (end == std::string::npos)
                    end = notes.size();
                std::string line = Trimmed(notes.substr(start, end - start));
                start = end + 1;
                if (line.empty())
                    continue;
                if (line[0] == '#')
                {
                    const size_t text = line.find_first_not_of('#');
                    line = text == std::string::npos ? std::string() : Trimmed(line.substr(text));
                    if (!first)
                        ImGui::Dummy(ImVec2(0, ui::space::XS));
                    ui::PushText(ui::TextStyle::Body, true);
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::Col(ui::color::Ink));
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(line.c_str()); // never ui::Text: a "##" in the notes is text, not an ID
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                    ui::PopText();
                }
                else
                {
                    const bool bullet = line.size() > 2 && (line[0] == '-' || line[0] == '*') && line[1] == ' ';
                    if (bullet)
                    {
                        line = Trimmed(line.substr(2));
                        const ImVec2 at = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddCircleFilled(
                            ImVec2(at.x + ui::space::XS / 2, at.y + ui::CapMiddle(ui::TextStyle::Body)), 2.0f,
                            ui::Col(ui::color::InkSoft));
                        ImGui::SetCursorScreenPos(ImVec2(at.x + ui::space::M, at.y));
                    }
                    ui::PushText(ui::TextStyle::Body);
                    ImGui::PushStyleColor(ImGuiCol_Text, ui::Col(ui::color::InkSoft));
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextUnformatted(line.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                    ui::PopText();
                }
                first = false;
            }
            ImGui::PopStyleVar();
            ui::EndCard();
            // Release page at the left of the actions' row.
            const ui::ModalResult result = ui::ModalActions(&open, Tr("Update now"), Tr("Close"));
            const float actionsY = ImGui::GetItemRectMin().y, afterY = ImGui::GetCursorPosY();
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetCursorStartPos().x, actionsY));
            if (ui::Button(Tr("Release page"), { .kind = ui::ButtonKind::Ghost,
                                                   .icon = ui::Icon::External,
                                                   .disabled = state.launcher.pageUrl.empty() }))
                SDL_OpenURL(state.launcher.pageUrl.c_str());
            ImGui::SetCursorPosY(afterY);
            ImGui::Dummy(ImVec2(0, 0));
            switch (result)
            {
            case ui::ModalResult::Confirm: UpdateLauncherNow(); break;
            default: break;
            }
            ui::EndModal();
        }
        if (!open)
            m_whatsNewOpen = false;
    }

    // Settings > Launcher: one check a day when it is on, nothing else is ever sent.
    void App::DrawUpdates()
    {
        const UpdateManager::State state = m_updates.Snapshot();
        const bool checking = state.phase == UpdateManager::Phase::Checking;
        if (!ui::BeginSettings("##updates"))
            return;
        std::string note;
        if (!m_updateDoneNote.empty())
            note = m_updateDoneNote;
        else if (int64_t last = SettingInt(m_config, "update", "last_check", 0); last > 0)
            note = Tf("Last checked {}", ui::FormatWhen(std::time_t(last), std::time_t(Now())));
        ui::SettingRow(Tr("Check for updates"), note.empty() ? nullptr : note.c_str());
        ui::ButtonOptions check;
        check.loading = checking;
        check.disabled = m_updates.Busy() && !checking;
        if (ui::Button(checking ? Tr("Checking...") : Tr("Check now"), check) && !m_updates.Busy())
            StartUpdateCheck(true);
        ui::SettingRow(Tr("Check at startup"), Tr("Looks for updates at each start."));
        bool automatic = ParseBool(SettingGet(m_config, "update", "auto_check", "1"));
        if (ui::Toggle("##autocheck", &automatic))
            Set("update", "auto_check", automatic ? "1" : "0");
        ui::SettingRow(Tr("Receive betas"), Tr("Offers test builds of the next version, then the final one."));
        bool betas = ReceiveBetas();
        if (ui::Toggle("##betas", &betas, m_updates.Busy()))
        {
            Set("update", "betas", betas ? "1" : "0");
            m_updateLater = false;
            StartUpdateCheck(false);
        }
        ui::EndSettings();
    }

    void App::DrawAbout()
    {
        ui::Text(ui::TextStyle::Display, "Arkeyan Launcher", ui::color::AccentFill, -1);
        ui::Text(
            ui::TextStyle::Body, Tf("Version {}, {}", LauncherVersion(), PlatformName()).c_str(), ui::color::InkSoft);
        if (IsBetaVersion(LauncherVersion()))
        {
            ImGui::SameLine(0, ui::space::XS);
            ui::Badge(Tr("Beta"), ui::color::Warning);
        }
        ui::Text(
            ui::TextStyle::Body, Tr("Native PC ports of Skylanders. No game code or art: it comes from your disc."));

        ui::SectionHeader(Tr("Credits"));
        ui::Text(ui::TextStyle::Body, Tr("Designed by dorydev, made by mflr0, UI/UX by Gumelia."), ui::color::InkSoft);
        ui::Text(ui::TextStyle::Body,
            Tr("Built with SDL 3, Dear ImGui, {fmt}, stb, miniz, XenonRecomp, Outfit and Bungee. MIT licensed."),
            ui::color::InkSoft);
        ui::Text(ui::TextStyle::Body,
            Tr("Games: XenonRecomp, XenosRecomp, plume, SDL 3, Dear ImGui, FFmpeg, parts of Xenia. Figure format "
               "learned from Dolphin and RPCS3."),
            ui::color::InkSoft);
        ImGui::Dummy(ImVec2(0, ui::space::XXS));
        // Each opens a web page: the External icon, as Release page's; the row's gap is the buttons' (S).
        const ui::ButtonOptions web{ .icon = ui::Icon::External };
        if (ui::Button(Tr("GitHub"), web))
            SDL_OpenURL(Project);
        ImGui::SameLine(0, ui::space::S);
        if (ui::Button(Tr("Licenses"), web))
            SDL_OpenURL((std::string(Project) + "/blob/main/THIRD_PARTY_NOTICES.md").c_str());
        ImGui::SameLine(0, ui::space::S);
        if (ui::Button(Tr("Report a bug"), web))
            SDL_OpenURL((std::string(Project) + "/issues/new").c_str());
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        ui::Text(ui::TextStyle::Caption,
            Tr("Skylanders, Spyro and Activision are trademarks of Activision Publishing, Inc. This project is not "
               "affiliated with or endorsed by Activision."),
            ui::color::Dim);
    }

    // The update screens (SKY_LAUNCHER_TEST_UPDATE, SKY_LAUNCHER_UPDATE_URL on a fake release): the banner, What's new, About
    // before and after a manual check, and a failure.
    void App::TestUpdateScript(uint64_t now)
    {
        if (const char* mode = LauncherEnv("SKY_LAUNCHER_TEST_UPDATE");
            mode != nullptr && std::string_view(mode) == "downgrade")
        {
            if (m_testBegan == 0)
                m_testBegan = now;
            if (now - m_testBegan > 15000)
            {
                m_testFailures++;
                m_running = false;
                return;
            }
            switch (m_testStep)
            {
            case 0:
                if (!m_updates.Snapshot().checked && !m_updates.Busy())
                    StartUpdateCheck(true);
                if (m_updates.Snapshot().checked && !m_updates.Busy())
                    m_testAt = now + 300;
                break;
            case 1:
            {
                // A channel switch also offers an older game and requests that release's tools.
                const auto disc = m_s->disc;
                const auto installed = m_s->installed;
                const auto kit = m_s->kit;
                const auto updates = m_gameUpdates;
                m_s->disc = DiscState::Ready;
                m_s->installed.runtime = "synthetic";
                m_s->installed.gameVersion = "0.8.1";
                m_s->kit = InstallKit{};
                m_s->kit->tag = "v0.9.8b1";
                m_s->kit->games = { m_s->game.id };
                m_gameUpdates = { { m_s->game.id, "0.8.0", "v0.9.7" } };
                if (!UpdateAvailable() || WantedKitTag(*m_s) != "v0.9.7" || !KitNeedsFetch(*m_s))
                    m_testFailures++;
                m_s->installed.gameVersion = "0.8.0";
                if (UpdateAvailable())
                    m_testFailures++;
                m_s->disc = disc;
                m_s->installed = installed;
                m_s->kit = kit;
                m_gameUpdates = updates;
                UpdateLauncherNow();
                if (!m_downgradeOpen || m_updates.Busy())
                    m_testFailures++;
                m_testAt = now + 300;
                break;
            }
            case 2:
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
                ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
                m_testAt = now + 300;
                break;
            default:
                if (m_downgradeOpen || m_updates.Busy() || m_updates.Snapshot().phase != UpdateManager::Phase::Idle)
                    m_testFailures++;
                m_running = false;
                break;
            }
            return;
        }
        switch (m_testStep)
        {
        case 0:
            m_page = Page::Play;
            if (!m_updateWasBusy && !m_updates.Snapshot().checked)
                StartUpdateCheck(true);
            else if (!m_updates.Busy())
                m_testAt = now + 400;
            break;
        case 1:
            m_whatsNewOpen = true;
            m_testAt = now + 500;
            break;
        case 2:
            m_whatsNewOpen = false;
            m_page = Page::Settings;
            m_tab = SettingsTab::Launcher; // the updates are in the launcher's tab
            m_updateDoneNote.clear();
            m_testAt = now + 500;
            break;
        case 3:
            m_updateDoneNote = Tr("Arkeyan Launcher is up to date.");
            m_testAt = now + 400;
            break;
        case 4:
            m_updates.UpdateLauncher(); // a fake release without SHA256SUMS: refused; with one, the update is made
            m_updateWasBusy = true;
            m_page = Page::Play;
            if (const auto phase = m_updates.Snapshot().phase;
                phase == UpdateManager::Phase::Failed || phase == UpdateManager::Phase::Done)
                m_testAt = now + 400;
            break;
        default: m_running = false; break;
        }
    }
}
