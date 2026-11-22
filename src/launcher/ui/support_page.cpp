#include "shared/runtime_names.h"
// Support in Settings: the game's logs (Copy game log, Open logs folder), "Report a problem" (one text file with a
// game's logs and versions to save, then GitHub to attach it to; nothing is uploaded) and "Test the portal"
// (docs/support.md).
#include "launcher/problem_report.h"
#include "launcher/report.h"
#include "launcher/ui/launcher_app.h"
#include "launcher/game_names.h"
#include "launcher/ui/components/format.h"
#include "shared/collection/datadir.h"
#include <fmt/format.h>
#include <fstream>
#include <memory>
#include <mutex>

namespace launcher_ui
{
    namespace
    {
        // The save dialog answers on a thread of its own: the callback writes the file and leaves the outcome here,
        // the window tells the player on its next frame.
        std::mutex g_saveMutex;
        bool g_saveOk = false, g_saveFailed = false;
        std::string g_savedText, g_savedName;
    }

    void SDLCALL SaveReport(void* userdata, const char* const* files, int)
    {
        std::unique_ptr<std::string> text(static_cast<std::string*>(userdata));
        std::lock_guard lock(g_saveMutex);
        if (files == nullptr)
        {
            g_saveFailed = true;
            return;
        }
        if (files[0] == nullptr) // cancelled
            return;
        std::ofstream out(
            datadir::FromText(files[0]), std::ios::binary | std::ios::trunc); // UTF-8: accents in a Windows user name
        out.write(text->data(), std::streamsize(text->size()));
        out.close();
        if (out)
        {
            g_saveOk = true;
            g_savedText = *text;
            g_savedName = datadir::Text(datadir::FromText(files[0]).filename());
        }
        else
            g_saveFailed = true;
    }

    void App::OpenProblemReport()
    {
        m_reportGame = m_s->game.id;
        m_reportDetails = false;
        BuildProblemReport();
        m_reportOpen = true;
    }

    // One text file with the picked game's last logs, the settings and the versions.
    void App::BuildProblemReport()
    {
        const GameInfo* game = FindGame(m_reportGame);
        const GameSlot& slot = game != nullptr ? SlotFor(*game) : *m_s;
        launcher_report::Sources sources = CurrentReportSources(m_configPath, m_config, slot.game.id, slot.discPath);
        if (!slot.installed.kitVersion.empty())
            sources.versions.kit = slot.installed.kitVersion;
        m_reportText = launcher_report::BuildReport(sources);
        m_reportFile.clear();
    }

    void App::DrawProblemReport()
    {
        // Tell the player how the save went, whatever the dialog's thread answered.
        {
            std::lock_guard lock(g_saveMutex);
            if (g_saveOk)
            {
                Toast(Tr("Report saved"), ui::ToastKind::Success);
                g_saveOk = false;
                if (g_savedText == m_reportText) // not another game's, picked while the save dialog was up
                    m_reportFile = g_savedName;
            }
            else if (g_saveFailed)
            {
                Toast(Tr("Couldn't save the report"), ui::ToastKind::Error);
                g_saveFailed = false;
            }
        }

        if (!ui::BeginModal("##problemreport", Tr("Report a problem"), &m_reportOpen, ui::ModalSize::Large))
            return;
        const GameInfo* game = FindGame(m_reportGame);
        // Kept until EndSettings, which draws the labels.
        const std::string saveStep = fmt::format("1. {}", Tr("Save the report"));
        const std::string sendStep = fmt::format("2. {}", Tr("Open GitHub"));
        if (ui::BeginSettings("##reportsteps"))
        {
            ui::SettingRow(Tr("Game"));
            const char* shown = GameName(m_reportGame, GameNameForm::Short);
            if (ui::BeginSelect("##reportgame", shown != nullptr ? shown : ""))
            {
                for (const GameInfo& each : g_games)
                {
                    if (FindEnabledGame(each.id, m_config) == nullptr)
                        continue;
                    const char* name = GameName(each.id, GameNameForm::Short);
                    if (ui::SelectOption(name != nullptr ? name : each.name, m_reportGame == each.id) &&
                        m_reportGame != each.id)
                    {
                        m_reportGame = each.id;
                        game = &each;
                        BuildProblemReport();
                    }
                }
                ui::EndSelect();
            }
            const bool saved = !m_reportFile.empty();
            ui::SettingRow(saveStep.c_str(), Tr("Anonymous: your names are removed."));
            ui::ButtonOptions save;
            save.kind = saved ? ui::ButtonKind::Secondary : ui::ButtonKind::Primary;
            // The same file as the game's F3 menu writes: on the Desktop, else in the logs folder.
            if (ui::Button(Tr("Save to Desktop"), save))
            {
                const SavedReport written = SaveProblemReportText(m_reportText);
                m_reportFile = written.file.empty() ? std::string() : datadir::Text(written.file.filename());
                if (m_reportFile.empty())
                    Toast(Tr("Couldn't save the report"), ui::ToastKind::Error);
                else
                    Toast(Tf("Report saved: {}", m_reportFile), ui::ToastKind::Success);
            }
            if (ImGui::IsWindowAppearing())
                ImGui::SetItemDefaultFocus();
            ui::SettingRow(sendStep.c_str(), Tr("Attach the saved report to the issue."));
            ui::ButtonOptions send;
            send.kind = saved ? ui::ButtonKind::Primary : ui::ButtonKind::Secondary;
            send.icon = ui::Icon::External;
            if (ui::Button(Tr("Open GitHub"), send) && game != nullptr)
                OpenProblemIssue(*game, m_reportFile);
            ui::EndSettings();
        }
        if (!m_reportFile.empty())
            ui::Text(ui::TextStyle::Caption, Tf("Report saved: {}", m_reportFile).c_str(), ui::color::Dim);

        if (ui::BeginDisclosure("##reportcontents", Tr("See what the report contains"), &m_reportDetails))
        {
            // What the window leaves under the steps, less the copy's row, the fold's end and the actions' row.
            const float room = ImGui::GetIO().DisplaySize.y - 2 * ui::space::XL - ImGui::GetCursorPosY() -
                               2 * ui::height::Normal - 5 * ui::space::S - ui::space::XS - ui::space::L -
                               ui::ModalLegendHeight();
            ui::BeginCard("##reporttext",
                ImVec2(0, std::clamp(room, 2.0f * ui::height::Large, 6.0f * ui::height::Large)),
                { .inset = true, .padding = ui::space::M });
            ImGui::SetScrollX(0.0f);
            ImGui::PushFont(m_monoFont, ui::type::Caption); // logs keep the monospaced font (null: the text font)
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(m_reportText.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            ui::EndCard();
            if (ui::Button(Tr("Copy")))
            {
                SDL_SetClipboardText(m_reportText.c_str());
                Toast(Tr("Report copied"), ui::ToastKind::Success);
            }
            ImGui::SameLine(0, ui::space::S);
            if (ui::Button(Tr("Save as...")))
            {
                const std::string start = datadir::Text(GameLogFolder() / launcher_report::FileName());
                static const SDL_DialogFileFilter Filters[] = { { "Text file", "txt" } };
                SDL_ShowSaveFileDialog(SaveReport, new std::string(m_reportText), m_window, Filters, 1, start.c_str());
            }
            ui::EndDisclosure();
        }
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        // Not ModalActions: alone there, Close would take the first focus from step 1.
        const char* close = Tr("Close");
        ui::AlignRight(ui::ButtonWidth(close));
        if (ui::Button(close))
        {
            m_reportOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ui::EndModal();
    }

    // The "Test the portal" row, drawn inside the open settings table; DrawPortalResult shows what the test found under it.
    void App::DrawPortalTest()
    {
        const std::string runtime = !m_s->installed.runtime.empty() ? m_s->installed.runtime : m_s->ownRuntime;
        const bool testing = !m_probe.done;
        ui::SettingRow(Tr("Test the portal"), runtime.empty() ? Tr("Install the game first.")
                                              : !m_probe.ran  ? Tr("Plug in the portal, then test it.")
                                                              : nullptr);
        ui::ButtonOptions options;
        options.loading = testing;
        options.disabled = runtime.empty();
        if (ui::Button(testing ? Tr("Testing...") : Tr("Test"), options) && !testing)
            m_probe.Start(runtime);
    }

    void App::DrawPortalResult()
    {
        if (!m_probe.ran || !m_probe.done)
            return;
        const PortalProbeResult& result = m_probe.result;
        const std::string mode = SettingGet(m_config, "portal", "mode", "auto");
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        if (!result.error.empty())
        {
            ui::StatusPill(ui::Status::Error, Tr("The test failed"));
            ui::Text(ui::TextStyle::Caption, result.error.c_str(), ui::color::Dim);
            return;
        }
        const char* spyro = GameName("ssa");
        for (const PortalProbeEntry& entry : result.portals)
        {
            std::string name = entry.product.empty()
                                   ? std::string(entry.kind == "usb" ? Tr("Xbox portal") : Tr("Portal"))
                                   : entry.product;
            const std::string problem = PortalStatusText(entry);
            const ui::Status pill = !entry.opened     ? ui::Status::Error
                                    : problem.empty() ? ui::Status::Success
                                                      : ui::Status::Warning;
            ui::StatusPill(pill, !entry.opened     ? Tr("Found, can't be opened")
                                 : problem.empty() ? Tr("Found")
                                                   : Tr("Found, not answering"));
            ImGui::SameLine(0, ui::space::XS);
            ImGui::AlignTextToFramePadding();
            ui::Text(ui::TextStyle::Body,
                fmt::format("{} ({} {}:{})", name, entry.kind == "usb" ? "USB" : "HID", entry.vid, entry.pid).c_str(),
                ui::color::Ink, -1);
            if (!entry.model.empty())
            {
                // The model from the id the portal answered to the game's reset, and what the game thinks of it.
                ui::Text(
                    ui::TextStyle::Caption, Tf("Model: {} (id {})", entry.model, entry.id).c_str(), ui::color::Dim);
                if (entry.verdict == "works")
                    ui::Text(ui::TextStyle::Caption, Tf("{} accepts this portal.", spyro).c_str(), ui::color::InkSoft);
                else if (entry.verdict == "warns" || entry.verdict == "rejected")
                    ui::Text(ui::TextStyle::Caption,
                        (entry.verdict == "rejected"
                                ? Tf("{} rejects this portal: {}.", spyro, entry.verdictReason)
                                : Tf("{} warns about this portal: {}.", spyro, entry.verdictReason))
                            .c_str(),
                        ui::color::InkSoft);
            }
            if (!problem.empty())
                ui::Text(ui::TextStyle::Caption, problem.c_str(), ui::color::Dim);
            if (const std::string commands = PortalDriverCommands(entry); !commands.empty())
            {
                ImGui::PushID(&entry);
                if (ui::Button(Tr("Copy commands")))
                {
                    SDL_SetClipboardText(commands.c_str());
                    Toast(Tr("Commands copied"), ui::ToastKind::Success);
                }
                ImGui::PopID();
            }
        }
        for (const std::string& note : result.notes)
            ui::Text(ui::TextStyle::Caption, note.c_str(), ui::color::Dim);
        bool good = false;
        const std::string verdict = PortalVerdict(mode, result, good);
        ui::StatusPill(good ? ui::Status::Success : ui::Status::Neutral, verdict.c_str());
        // Every portal with each write method's line, for a GitHub issue.
        ImGui::SameLine(0, ui::space::S);
        if (ui::Button(Tr("Copy")))
        {
            SDL_SetClipboardText(PortalProbeText(mode, result).c_str());
            Toast(Tr("Test result copied"), ui::ToastKind::Success);
        }
    }

    void App::DrawLogs()
    {
        const std::filesystem::path folder = GameLogFolder();
        // Which run Copy game log carries, looked up again each second (a game may be writing it).
        const uint64_t now = SDL_GetTicks();
        if (now >= m_lastRunLookAt)
        {
            m_lastRun = LastGameLog();
            m_lastRunLookAt = now + 1000;
        }
        const bool any = !m_lastRun.file.empty();
        if (!ui::BeginSettings("##support"))
            return;
        ui::SettingRow(Tr("Problem report"), Tr("Logs and settings, without your names, for a GitHub issue."));
        ui::ButtonOptions report;
        report.kind = ui::ButtonKind::Primary;
        if (ui::Button(Tr("Report a problem"), report))
            OpenProblemReport();
        const char* lastGame = GameName(m_lastRun.game, GameNameForm::Short);
        const std::string lastRun =
            !any ? Tr("No game log yet.")
            : lastGame != nullptr
                ? Tf("Last run: {}, {}", lastGame, ui::FormatWhen(m_lastRun.written, std::time(nullptr)))
                : Tf("Last run: {}", ui::FormatWhen(m_lastRun.written, std::time(nullptr)));
        ui::SettingRow(Tr("Game log"), lastRun.c_str());
        ui::ButtonOptions copy;
        copy.disabled = !any;
        if (ui::Button(Tr("Copy game log"), copy))
        {
            SDL_SetClipboardText(
                launcher_report::Anonymous(GameLogText(), launcher_report::CurrentIdentity("")).c_str());
            Toast(Tr("Game log copied"), ui::ToastKind::Success);
        }
        ui::SettingRow(Tr("Logs folder"), nullptr);
        ui::ButtonOptions open;
        open.icon = ui::Icon::Folder;
        if (ui::Button(Tr("Open logs folder"), open) && !OpenFolder(folder))
            Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
        ui::EndSettings();
    }
}
