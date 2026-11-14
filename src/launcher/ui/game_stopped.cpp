// "The game stopped unexpectedly": the crash dialog of a game started from the launcher's window, in the window's own style
// (issue #2). Report writes the report file and opens a GitHub issue to attach it to; Copy log copies the game's log and keeps
// the dialog; Close. A game started from its shortcut without the window keeps the system message box (ShowGameStopped).
#include "launcher/game_run.h"
#include "launcher/problem_report.h"
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include <SDL3/SDL.h>

namespace launcher_ui
{
    void App::DrawGameStopped()
    {
        if (!m_gameStopped)
            return;
        const GameInfo* game = FindGame(m_gameStopped->game);
        if (game == nullptr)
        {
            m_gameStopped.reset();
            return;
        }
        bool open = true;
        if (ui::BeginModal("##gamestopped", Tr("The game stopped unexpectedly"), &open, ui::ModalSize::Medium))
        {
            ui::Text(ui::TextStyle::Body,
                Tf("{} stopped unexpectedly (exit code {}).\n\nPress Report to save a report file with the log and where "
                   "the game stopped.",
                    game->name, m_gameStopped->exitCode)
                    .c_str(),
                ui::color::Ink);
            ImGui::Dummy(ImVec2(0, ui::space::S));
            const ui::ModalResult result = ui::ModalActions(&open, Tr("Report"), Tr("Close"));
            if (result == ui::ModalResult::Confirm)
                ReportGameStopped(*game, m_gameStopped->exitCode, m_configPath, m_config, m_gameStopped->iso);
            // Copy log, at the left of the actions' row: the dialog stays open.
            const float actionsY = ImGui::GetItemRectMin().y, afterY = ImGui::GetCursorPosY();
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetCursorStartPos().x, actionsY));
            if (ui::Button(Tr("Copy log"), { .kind = ui::ButtonKind::Ghost }))
            {
                SDL_SetClipboardText(GameLogText().c_str());
                Toast(Tr("Log copied"), ui::ToastKind::Success);
            }
            ImGui::SetCursorPosY(afterY);
            ImGui::Dummy(ImVec2(0, 0));
            ui::EndModal();
        }
        if (!open)
            m_gameStopped.reset();
    }
}
