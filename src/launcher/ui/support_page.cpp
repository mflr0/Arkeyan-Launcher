#include "shared/runtime_names.h"
// Support in Settings: the game's logs (Copy game log, Open logs folder), "Report a problem" (one text file with the
// logs and the player's versions, shown before anything is saved or copied; nothing is uploaded) and "Test the portal"
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
        }
        else
            g_saveFailed = true;
    }

    // One text file with the last logs, the settings and the versions; it is shown before it can be saved or copied.
    void App::OpenProblemReport()
    {
        launcher_report::Sources sources = CurrentReportSources(m_configPath, m_config, m_s->game.id, m_s->discPath);
        if (!m_s->installed.kitVersion.empty())
            sources.versions.kit = m_s->installed.kitVersion;
        m_reportText = launcher_report::BuildReport(sources);
        m_reportOpen = true;
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
            }
            else if (g_saveFailed)
            {
                Toast(Tr("Couldn't save the report"), ui::ToastKind::Error);
                g_saveFailed = false;
            }
        }

        if (!ui::BeginModal("##problemreport", Tr("Report a problem"), &m_reportOpen, ui::ModalSize::Large))
            return;
        ui::Text(ui::TextStyle::Caption,
            Tr("Anonymous: your names are removed. Save it, then add it to a GitHub issue."), ui::color::Dim);
        // The room under the text is the title, the line above and the actions (Normal high); the rest is the report.
        const float room =
            ImGui::GetIO().DisplaySize.y - 2 * ui::space::XL - 3 * ui::height::Large - ui::ModalLegendHeight();
        ui::BeginCard("##reporttext",
            ImVec2(0, std::clamp(room, 3.0f * ui::height::Large, 8.0f * ui::height::Large - ui::ModalLegendHeight())),
            { .inset = true, .padding = ui::space::M });
        ImGui::SetScrollX(0.0f);
        ImGui::PushFont(m_monoFont, ui::type::Caption); // logs keep the monospaced font (null: the text font)
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(m_reportText.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ui::EndCard();
        // Save to Desktop is the action (the same file the game's F3 menu writes: on the Desktop, else in the logs folder);
        // Save as... and Copy stay at the left of the same row.
        ui::ModalResult result = ui::ModalActions(&m_reportOpen, Tr("Save to Desktop"), Tr("Close"));
        const float afterY = ImGui::GetCursorPosY();
        ImGui::SetCursorScreenPos(ImVec2(
            ImGui::GetWindowPos().x + ImGui::GetCursorStartPos().x, ImGui::GetItemRectMin().y)); // on the actions' row
        if (ui::Button(Tr("Save as...")))
        {
            std::filesystem::path folder = GameLogFolder();
            std::string name = launcher_report::FileName();
            std::string start = datadir::Text(folder / name);
            static const SDL_DialogFileFilter Filters[] = { { "Text file", "txt" } };
            SDL_ShowSaveFileDialog(SaveReport, new std::string(m_reportText), m_window, Filters, 1, start.c_str());
        }
        ImGui::SameLine(0, ui::space::S);
        if (ui::Button(Tr("Copy")))
        {
            SDL_SetClipboardText(m_reportText.c_str());
            Toast(Tr("Report copied"), ui::ToastKind::Success);
        }
        ImGui::SetCursorPosY(afterY);
        ImGui::Dummy(ImVec2(0, 0)); // the cursor went back and forth: an item ends it
        if (result == ui::ModalResult::Confirm)
        {
            SavedReport saved = SaveProblemReportText(m_reportText);
            if (saved.file.empty())
                Toast(Tr("Couldn't save the report"), ui::ToastKind::Error);
            else
                Toast(Tf("Report saved: {}", datadir::Text(saved.file.filename())), ui::ToastKind::Success);
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
            SDL_SetClipboardText(GameLogText().c_str());
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
