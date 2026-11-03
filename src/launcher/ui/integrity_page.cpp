// The file check (docs/install.md, "File check"): started with the window, on a thread (integrity.h); what it finds is
// shown in a dialog that names the file, says what an antivirus does and where to exclude, and offers Repair: the kit
// downloaded and unpacked again over itself, or the game built again from the player's disc.
#include "launcher/ui/launcher_app.h"
#include "shared/collection/datadir.h"
#include <algorithm>
#include <fmt/format.h>
#include <fstream>

namespace launcher_ui
{
    namespace
    {
        bool SameFolder(const std::filesystem::path& a, const std::filesystem::path& b)
        {
            return a.lexically_normal() == b.lexically_normal();
        }
    }

    // The kit of every enabled game (the one its INSTALL would use) and every game's folder.
    void App::StartIntegrityCheck()
    {
        std::vector<IntegrityTarget> targets;
        m_checkedKits.clear();
        for (const GameInfo& game : g_games)
        {
            if (FindEnabledGame(game.id, m_config) == nullptr)
                continue;
            if (std::optional<InstallKit> kit = FindInstallKit(m_config, game.id))
            {
                const std::filesystem::path folder = kit->ssabuild.parent_path();
                bool known = false;
                for (const InstallKit& checked : m_checkedKits)
                    known = known || SameFolder(checked.ssabuild.parent_path(), folder);
                if (!known)
                {
                    m_checkedKits.push_back(*kit);
                    targets.push_back({ "kit", folder });
                }
            }
            targets.push_back({ game.id, LauncherDataFolder() / datadir::GameFolderName(game.id) });
        }
        m_integrity.Start(std::move(targets));
        m_integrityPolled = false;
    }

    void App::PollIntegrity()
    {
        if (!m_integrityPolled && !m_integrity.Running())
        {
            m_integrityPolled = true;
            IntegrityCheck::Report report = m_integrity.Snapshot();
            fprintf(stderr, "launcher: file check: %zu files, %zu read (%.0f MB) in %.2f s, %zu with a problem\n", report.files,
                report.hashed, double(report.hashedBytes) / (1 << 20), report.seconds, report.findings.size());
            m_findings = std::move(report.findings);
            m_integrityOpen = !m_findings.empty();
            if (!m_kitRepaired.empty() && std::none_of(m_findings.begin(), m_findings.end(), [&](const IntegrityFinding& finding) { return SameFolder(finding.folder, m_kitRepaired); }))
                Toast(Tr("The game tools are repaired"), ui::ToastKind::Success);
            m_kitRepaired.clear();
        }
        if (!m_kitRepairPolled && !m_kitRepair.Running())
        {
            m_kitRepairPolled = true;
            KitDownloader::Snapshot_ result = m_kitRepair.Snapshot();
            if (result.state == KitDownloader::State::Done)
            {
                RefreshInstall();
                m_kitRepaired = result.folder;
                StartIntegrityCheck(); // repaired if it finds nothing there; else the dialog again (an exclusion is needed)
            }
            else if (result.state == KitDownloader::State::Failed)
                Toast(Tf("Couldn't repair the game tools: {}", result.error), ui::ToastKind::Error);
        }
    }

    void App::ShowFinding(IntegrityFinding finding)
    {
        std::erase_if(m_findings, [&](const IntegrityFinding& other) { return SameFolder(other.folder, finding.folder); });
        m_findings.insert(m_findings.begin(), std::move(finding));
        m_integrityOpen = true;
    }

    bool App::KitIntact()
    {
        if (!m_s->kit)
            return true;
        IntegrityFinding finding = CheckFolder({ "kit", m_s->kit->ssabuild.parent_path() });
        if (finding.problems.empty())
            return true;
        bool known = false;
        for (const InstallKit& checked : m_checkedKits)
            known = known || SameFolder(checked.ssabuild.parent_path(), finding.folder);
        if (!known)
            m_checkedKits.push_back(*m_s->kit);
        ShowFinding(std::move(finding));
        return false;
    }

    void App::RepairFinding(const IntegrityFinding& finding)
    {
        if (finding.id == "kit")
        {
            if (AnyInstalling() || m_kitRepair.Running())
            {
                Toast(Tr("Wait for the installation to finish"), ui::ToastKind::Error);
                return;
            }
            const InstallKit* kit = nullptr;
            for (const InstallKit& checked : m_checkedKits)
                if (SameFolder(checked.ssabuild.parent_path(), finding.folder))
                    kit = &checked;
            // The release it came from: its kit/<tag>/ folder, else its kit.txt, else the launcher's own.
            const std::string tag = kit != nullptr && !kit->tag.empty() ? kit->tag
                : kit != nullptr && !kit->release.empty()                ? "v" + kit->release
                                                                          : "v" + LauncherVersion();
            m_kitRepair.Start(SettingGet(m_config, "update", "kit_repo", "mflr0/Arkeyan-Launcher"), tag, DownloadedKitsFolder(), finding.folder);
            m_kitRepairPolled = false;
            Toast(Tr("Downloading the game tools again"), ui::ToastKind::Info);
            return;
        }
        const GameInfo* game = FindGame(finding.id);
        if (game == nullptr)
            return;
        GameSlot& slot = SlotFor(*game);
        if (slot.disc != DiscState::Ready)
        {
            SelectGame(*game);
            m_page = Page::Play;
            Toast(slot.disc == DiscState::Verifying ? std::string(Tr("The disc is still being checked: try again in a moment"))
                                                    : Tf("Repair needs the disc of {}", GameName(game->id, GameNameForm::Short)),
                ui::ToastKind::Error);
            return;
        }
        // The damaged files go first, so that the build writes them again whatever it takes as up to date.
        for (const filelist::Problem& problem : finding.problems)
            if (problem.kind == filelist::Problem::Kind::Changed)
            {
                std::error_code error;
                std::filesystem::remove(finding.folder / datadir::FromText(problem.path), error);
            }
        // As UPDATE on its page: the game's page with its progress card.
        SelectGame(*game);
        m_page = Page::Play;
        StartInstall();
    }

    void App::DrawIntegrity()
    {
        if (!m_integrityOpen || m_findings.empty())
            return;
        const IntegrityFinding finding = m_findings.front(); // a copy: Repair changes the list
        const bool isKit = finding.id == "kit";
        bool chosen = false;
        for (const InstallKit& checked : m_checkedKits)
            if (SameFolder(checked.ssabuild.parent_path(), finding.folder))
                chosen = checked.chosen;
        const filelist::Problem& first = finding.problems.front();
        const bool missing = first.kind == filelist::Problem::Kind::Missing;
        const std::string name = isKit ? std::string() : std::string(GameName(finding.id));
        std::string what = isKit ? (missing ? Tf("The game tools are missing a file: {}", first.path) : Tf("A file of the game tools was changed: {}", first.path))
                                 : (missing ? Tf("{0} is missing a file: {1}", name, first.path) : Tf("A file of {0} was changed: {1}", name, first.path));
        if (finding.problems.size() == 2)
            what += " " + std::string(Tr("And 1 other file."));
        else if (finding.problems.size() > 2)
            what += " " + Tf("And {} other files.", finding.problems.size() - 1);
        const std::string how = chosen ? Tr("These are your own game tools: restore the file, or set them up again.")
            : isKit                    ? Tr("Repair downloads the game tools again.")
                                       : Tr("Repair builds the game again from your disc.");

        bool open = true, repaired = false;
        if (ui::BeginModal("##integrity", Tr("Files missing or changed"), &open, ui::ModalSize::Medium))
        {
            ui::Text(ui::TextStyle::Body, what.c_str(), ui::color::Ink, 0, true);
            ImGui::Dummy(ImVec2(0, ui::space::XS));
            ui::Text(ui::TextStyle::Body,
                Tr("An antivirus often does this: it may have put the file in quarantine. Restore it from the antivirus's quarantine, or add an exclusion for this folder:"),
                ui::color::InkSoft);
            ui::Text(ui::TextStyle::Body, datadir::Text(finding.folder.lexically_normal()).c_str(), ui::color::Ink);
            ImGui::Dummy(ImVec2(0, ui::space::XS));
            ui::Text(ui::TextStyle::Body, how.c_str(), ui::color::InkSoft);
            const ui::ModalResult result = ui::ModalActions(&open, chosen ? nullptr : Tr("Repair"), Tr("Close"));
            // The folder at the left of the actions' row.
            const float actionsY = ImGui::GetItemRectMin().y, afterY = ImGui::GetCursorPosY();
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetCursorStartPos().x, actionsY));
            if (ui::Button(Tr("Open the folder"), { .kind = ui::ButtonKind::Ghost, .icon = ui::Icon::External }) && !OpenFolder(finding.folder))
                Toast(Tr("Couldn't open the folder"), ui::ToastKind::Error);
            ImGui::SetCursorPosY(afterY);
            ImGui::Dummy(ImVec2(0, 0));
            ui::EndModal();
            repaired = result == ui::ModalResult::Confirm;
        }
        if (!open)
        {
            // Closed (or repaired): this one is left for this session; the next one, if any, is shown.
            m_findings.erase(m_findings.begin());
            m_integrityOpen = false;
            if (repaired)
                RepairFinding(finding); // may show the kit's own finding (an install it blocked)
            m_integrityOpen = m_integrityOpen || !m_findings.empty();
        }
    }
}

namespace launcher_ui
{
    // The file check (SKY_LAUNCHER_TEST_KIT=integrity; SKY_LAUNCHER_BASE_PATH holds a kit of the launcher's release whose
    // files.sha256 lists Arkeyan-Builder, which an antivirus took; SKY_LAUNCHER_UPDATE_URL is a fake release of that tag whose kit,
    // "kit-repaired", has it): the check at start names the file; INSTALL is blocked with the same dialog; Repair unpacks
    // the release's kit over the damaged one; the install then writes the game's list; a game file changed afterwards is
    // named at the next check, and Repair builds the game again.
    void App::TestIntegrityScript(uint64_t now)
    {
        const GameInfo& ssa = *FindGame("ssa");
        GameSlot& slot = SlotFor(ssa);
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL integrity step %d: %s (line \"%s\", %zu findings)\n", m_testStep, what, SlotStatus(&slot).text.c_str(), m_findings.size());
                m_testFailures++;
            }
        };
        auto has = [&](const char* id, filelist::Problem::Kind kind, const std::string& path) {
            for (const IntegrityFinding& finding : m_findings)
                for (const filelist::Problem& problem : finding.problems)
                    if (finding.id == id && problem.kind == kind && problem.path == path)
                        return true;
            return false;
        };
        auto shoot = [&](const char* name) { // Frame() takes it, then goes to the next step
            m_toasts.Clear();
            m_testShot = fmt::format("{:02}-{}", m_testStep, name);
            m_testAt = now + 500;
        };
        auto checking = [&] { return m_integrity.Running() || !m_integrityPolled; };
        auto busy = [&] { return slot.installer.Running() || slot.kitDownload.Running() || slot.installView; };
        using Kind = filelist::Problem::Kind;
#if defined(_WIN32)
        const std::string builder = "Arkeyan-Builder.exe";
#else
        const std::string builder = "Arkeyan-Builder";
#endif
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 120000)
        {
            expect(false, "timeout");
            m_running = false;
            return;
        }
        if (m_testAt != 0) // a screenshot to take first
            return;
        switch (m_testStep)
        {
        case 0: // the disc checked, the check at start done
            m_page = Page::Play;
            if (m_testBegan == now)
            {
                SelectGame(ssa);
                return;
            }
            if (slot.disc == DiscState::Verifying || checking())
                return;
            expect(slot.disc == DiscState::Ready, "the disc is ready");
            expect(m_integrityOpen && m_findings.size() == 1 && has("kit", Kind::Missing, builder), "the check at start names the kit's missing file");
            shoot("integrity-kit");
            break;
        case 1: // closed; INSTALL: blocked, the same file named again
            m_findings.clear();
            m_integrityOpen = false;
            StartInstall();
            expect(!slot.installer.Running() && !slot.kitDownload.Running() && !slot.installView, "INSTALL does not start with a damaged kit");
            expect(m_integrityOpen && has("kit", Kind::Missing, builder), "and names the file instead");
            m_testStep++;
            break;
        case 2: // Repair
        {
            IntegrityFinding finding = m_findings.front();
            m_findings.erase(m_findings.begin());
            m_integrityOpen = false;
            RepairFinding(finding);
            expect(m_kitRepair.Running(), "Repair downloads the kit");
            m_testStep++;
            break;
        }
        case 3: // repaired and checked again: INSTALL now runs
            if (m_kitRepair.Running() || !m_kitRepairPolled || checking())
                return;
            expect(m_findings.empty() && !m_integrityOpen, "nothing is wrong any more");
            expect(slot.kit && slot.kit->version == "kit-repaired" && slot.kit->tag.empty(), "the release's kit is unpacked over the damaged one, in its place");
            StartInstall();
            expect(slot.installer.Running(), "INSTALL starts");
            m_testStep++;
            break;
        case 4: // installed: the game's list is written; a file of it is then changed
        {
            if (busy() || m_integrity.WritingList())
                return;
            const std::filesystem::path folder = GameInstallFolder(slot.check.xexSha256);
            expect(std::filesystem::exists(folder / filelist::ListName), "the install wrote the game's file list");
            const std::filesystem::path runtime = datadir::FromText(slot.installed.runtime);
            expect(!slot.installed.runtime.empty() && std::filesystem::exists(runtime), "the game is installed");
            std::ofstream(runtime, std::ios::binary | std::ios::app) << "cleaned by an antivirus\n";
            StartIntegrityCheck();
            m_testStep++;
            break;
        }
        case 5:
        {
            if (checking())
                return;
            const std::string name = datadir::Text(datadir::FromText(slot.installed.runtime).filename());
            expect(m_integrityOpen && m_findings.size() == 1 && has("ssa", Kind::Changed, name), "the next check names the changed game file");
            shoot("integrity-game");
            break;
        }
        case 6: // Repair: the game is built again
        {
            IntegrityFinding finding = m_findings.front();
            m_findings.erase(m_findings.begin());
            m_integrityOpen = false;
            RepairFinding(finding);
            expect(slot.installer.Running(), "Repair builds the game again");
            m_testStep++;
            break;
        }
        case 7:
            if (busy() || m_integrity.WritingList())
                return;
            StartIntegrityCheck();
            m_testStep++;
            break;
        case 8:
            if (checking())
                return;
            expect(m_findings.empty() && !m_integrityOpen, "the rebuilt game is whole");
            m_running = false;
            break;
        }
    }
}
