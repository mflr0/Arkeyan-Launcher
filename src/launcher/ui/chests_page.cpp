// Imaginite chests (the Chests button on Imaginators' Play page): the save slot played last and its pile, one counter per
// kind of chest, Add, and the chests waiting with Cancel (launcher/game_chests.h). docs/launcher_ui.md, "Imaginite chests".
#include "launcher/ui/launcher_app.h"
#include "launcher/game_chests.h"
#include "shared/collection/progress.h"
#include "shared/collection/save_backup.h"
#include <algorithm>
#include <cstdio>
#include <numeric>

namespace launcher_ui
{
    namespace
    {
        constexpr uint64_t RefreshMs = 1000;
    }

    // The pile and the waiting chests of the shown game, read again every second (the game may be running and adding).
    void App::RefreshChests(bool force)
    {
        const uint64_t now = SDL_GetTicks();
        if (!force && m_chestsSlot == m_s && now - m_chestsAt < RefreshMs)
            return;
        if (m_chestsSlot != m_s)
            m_chestCounts.clear();
        m_chests = ReadGameChests(progress::Folder(m_s->game.id), m_s->game.id);
        m_chestCounts.resize(ChestKinds(m_s->game.id).size(), 0);
        m_chestsSlot = m_s;
        m_chestsAt = now;
    }

    // A settings page's frame (the game's name, Back), one card: the slot and the room left, a slider per kind (each one up
    // to what the others leave), Add, then the waiting chests and Cancel.
    void App::DrawChests()
    {
        RefreshChests();
        const GameChests& chests = m_chests;
        const std::span<const ChestKindRow> kinds = ChestKinds(m_s->game.id);
        // One counter per kind, whatever set the counters last (a test, another game): the sliders index them by kind.
        if (m_chestCounts.size() != kinds.size())
            m_chestCounts.resize(kinds.size(), 0);
        const char* title = GameName(m_s->game.id, GameNameForm::Full);
        BeginSettingsPage("##chestscolumn", title != nullptr ? title : m_s->game.name, true, true);
        BeginCard("##chests", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        const bool open = chests.known && !chests.WaitingElsewhere();
        std::string subtitle;
        if (!chests.known)
            subtitle = Tr("Reach the Academy once in the game to add chests.");
        else
            subtitle =
                Tf("Save slot {} · {} in the pile · Room for {} more", chests.slot + 1, chests.pile, chests.room);
        ui::SectionHeader(Tr("Imaginite chests"), subtitle.c_str(), false);
        const int asked = std::accumulate(m_chestCounts.begin(), m_chestCounts.end(), 0);
        if (ui::BeginSettings("##chestkinds"))
        {
            if (chests.waiting > 0)
            {
                const std::string line = chests.WaitingElsewhere() ? Tf("{} chests waiting for save slot {}",
                                                                         chests.waiting, chests.waitingSlot + 1)
                                                                   : Tf("{} chests waiting", chests.waiting);
                ui::SettingRow(line.c_str());
                if (ui::Button(Tr("Cancel")))
                {
                    if (!CancelChests(progress::Folder(m_s->game.id)))
                        Toast(Tr("Too late: the game already added them"), ui::ToastKind::Error);
                    RefreshChests(true);
                }
            }
            for (size_t i = 0; i < kinds.size(); i++)
            {
                ui::SettingRow(Tr(kinds[i].label));
                int& count = m_chestCounts[i];
                const int max = std::max(count, count + chests.room - asked);
                count = std::clamp(count, 0, std::max(0, max));
                ui::SliderOptions options;
                options.disabled = !open || max == 0;
                const std::string id = "##chest" + std::string(kinds[i].key);
                ui::Slider(id.c_str(), &count, 0, std::max(1, max), options);
                count = std::clamp(count, 0, std::max(0, max));
            }
            ui::EndSettings();
        }

        const int total = std::accumulate(m_chestCounts.begin(), m_chestCounts.end(), 0);
        ImGui::Dummy(ImVec2(0, ui::space::XS));
        ui::ButtonOptions add;
        add.kind = ui::ButtonKind::Primary;
        add.icon = ui::Icon::Plus;
        add.disabled = !open || total == 0 || total > chests.room;
        const std::string addLabel = total == 1 ? std::string(Tr("Add 1 chest")) : Tf("Add {} chests", total);
        if (ui::Button(addLabel.c_str(), add))
        {
            if (AddChests(progress::Folder(m_s->game.id), m_s->game.id, m_chestCounts))
            {
                std::fill(m_chestCounts.begin(), m_chestCounts.end(), 0);
                Toast(Tr("Chests added: they appear at the Academy"), ui::ToastKind::Success);
            }
            else
                Toast(Tr("Couldn't add the chests"), ui::ToastKind::Error);
            RefreshChests(true);
        }
        if (open)
            ui::Text(
                ui::TextStyle::Caption, Tr("They appear the next time this save reaches the Academy."), ui::color::Dim);

        EndCard();
        if (m_testChests && m_testStep >= 4)
            ImGui::SetScrollY(ImGui::GetScrollMaxY()); // the screenshot test's Add button
        EndSettingsPage();
    }
}

namespace launcher_ui
{
    // SKY_LAUNCHER_TEST_CHESTS (with SKY_LAUNCHER_SHOTS, a scratch SKYLANDERS_DATA or SKY_SAVE_DIR; it shows Imaginators): the
    // Chests page before the game read a pile, with a pile of 4 and counters set, with chests waiting, and with chests waiting
    // for another slot. The files are written in the game's player folder.
    void App::TestChestsScript(uint64_t now)
    {
        // Without a disc the launcher opens on another game, which has no chests: the script shows Imaginators.
        if (m_testStep == 0)
            SelectGame(*FindGame("imaginators"));
        const std::span<const ChestKindRow> kinds = ChestKinds(m_s->game.id);
        if (kinds.empty())
        {
            fprintf(stderr, "FAIL chests step %d: %s has no chests\n", m_testStep, m_s->game.id);
            m_testFailures++;
            m_running = false;
            return;
        }
        const std::filesystem::path folder = progress::Folder(m_s->game.id);
        // A counter by its kind's key, so the script never indexes past the kinds the game has.
        auto setCount = [&](std::string_view key, int count) {
            m_chestCounts.resize(kinds.size(), 0);
            for (size_t i = 0; i < kinds.size(); i++)
                if (kinds[i].key == key)
                {
                    m_chestCounts[i] = count;
                    return;
                }
            fprintf(stderr, "FAIL chests step %d: no chest kind %.*s\n", m_testStep, int(key.size()), key.data());
            m_testFailures++;
        };
        auto write = [&](const char* name, std::string_view text) {
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            savebackup::WriteFileAtomic(folder / name, text.data(), text.size());
        };
        switch (m_testStep)
        {
        case 0:
        {
            std::error_code error;
            std::filesystem::remove(folder / "chests.txt", error);
            std::filesystem::remove(folder / "chests_add.txt", error);
            GoTo(Page::Chests);
            RefreshChests(true);
            m_testShot = "chests-1-unknown";
            m_testAt = now + 600;
            break;
        }
        case 1:
            write("chests.txt", "slot=0\npile=4\npile.imaginite=3\npile.gold=1\npile.other=0\nlimit=99\n");
            RefreshChests(true);
            m_chestCounts.assign(kinds.size(), 0);
            setCount("copper", 20);
            setCount("platinum", 5);
            m_testShot = "chests-2-pile";
            m_testAt = now + 600;
            break;
        case 2:
            AddChests(folder, m_s->game.id, m_chestCounts);
            std::fill(m_chestCounts.begin(), m_chestCounts.end(), 0);
            RefreshChests(true);
            m_testShot = "chests-3-waiting";
            m_testAt = now + 600;
            break;
        case 3:
            write("chests.txt", "slot=1\npile=0\npile.other=0\nlimit=99\n");
            RefreshChests(true);
            m_testShot = "chests-4-elsewhere";
            m_testAt = now + 600;
            break;
        case 4:
            write("chests.txt", "slot=0\npile=4\npile.imaginite=3\npile.gold=1\npile.other=0\nlimit=99\n");
            RefreshChests(true);
            setCount("imaginite", 3);
            m_testShot = "chests-5-add";
            m_testAt = now + 600;
            break;
        default: m_running = false;
        }
    }
}
