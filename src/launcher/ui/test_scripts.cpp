// The screenshot and check scripts (SKY_LAUNCHER_TEST_*, docs/launcher_ui.md "Screenshot test"): how the tests drive the
// window. Kept apart from the window's own code, which calls them only while a test variable is set.
#include "launcher/ui/launcher_ui.h"
#include "launcher/env.h"
#include "launcher/game_names.h"
#include "launcher/ui/collection_model.h"
#include "shared/collection/progress.h"
#include "shared/collection/save_backup.h"
#include "launcher/ui/text_fit.h"
#include "launcher/install_checklist.h"
#include "shared/art/game_art.h"
#include "launcher/problem_report.h"
#include "launcher/single_instance.h"
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/components/cards.h"
#include "shared/collection/datadir.h"
#include "shared/embedded_fonts.h"
#include "assets/launcher_assets.h"
#include "launcher/ui/theme/launcher_theme.h"
#include "shared/vfs.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h> // ImGui::FocusItem
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <mutex>
#include <stb_image.h>
#include <stb_image_write.h>
#include <thread>
#include <vector>

namespace launcher_ui
{
    // The states the sidebar test puts on a built, verified game by hand: the sidebar's line, the main button's label.
    struct StateCase
    {
        const char* name;
        const char* text;
        const char* button; // as drawn: an unpressable one has " (off)"
        void (*setup)(GameSlot&);
    };
    static const StateCase StateCases[] = {
        { "no-disc", "Needs your disc", "Choose disc", [](GameSlot& slot) { slot.disc = DiscState::None; } },
        { "checking", "Checking your disc", "CHECKING... (off)",
            [](GameSlot& slot) {
                slot.disc = DiscState::Verifying;
                slot.verify.done = false;
            } },
        { "disc-gone", "Disc not found", "Choose disc",
            [](GameSlot& slot) {
                slot.disc = DiscState::Failed;
                slot.check.status = GameSourceStatus::NotFound;
            } },
        { "unreadable", "Can't read the disc", "Choose disc",
            [](GameSlot& slot) {
                slot.disc = DiscState::Failed;
                slot.check.status = GameSourceStatus::Unreadable;
            } },
        { "wrong-disc", "Wrong disc", "Choose disc",
            [](GameSlot& slot) {
                slot.disc = DiscState::Failed;
                slot.check.status = GameSourceStatus::WrongGame;
            } },
        { "unknown-release", "Unknown release", "Choose disc",
            [](GameSlot& slot) {
                slot.disc = DiscState::Failed;
                slot.check.status = GameSourceStatus::WrongVersion;
            } },
        { "not-a-game-disc", "Not a game disc", "Choose disc",
            [](GameSlot& slot) {
                slot.disc = DiscState::Failed;
                slot.check.status = GameSourceStatus::NotXboxDisc;
            } },
        { "ready-to-install", "Ready to install", "INSTALL", [](GameSlot& slot) { slot.installed.runtime.clear(); } },
        { "update-available", "Update available", "UPDATE",
            [](GameSlot& slot) { slot.installed.gameVersion = "0.0.1"; } },
        { "ready", "Ready to play", "PLAY", [](GameSlot&) {} },
    };

    // The kit follows the launcher (SKY_LAUNCHER_TEST_KIT, with SKY_LAUNCHER_BASE_PATH holding an old kit and SKY_LAUNCHER_UPDATE_URL a
    // fake release of the launcher's own tag, its kit asset being the fake kit "kit-new"; both games' iso= for "old" and "giants"):
    //  old       INSTALL with a kit next to the launcher from an older release: the kit of the launcher's release is fetched
    //            first, and it is the one that builds the game;
    //  giants    INSTALL of Giants with a kit that does not build it (no giants in games=): the right kit first;
    //  updates   the game updates (TestUpdatesScript): a release that bumps Giants only;
    //  offline   the same with no release to reach;
    //  integrity the file check (TestIntegrityScript).
    void App::TestKitScript(uint64_t now)
    {
        const GameInfo& ssa = *FindGame("ssa");
        const GameInfo& giants = *FindGame("giants");
        if (m_testKit == "updates" || m_testKit == "offline")
        {
            TestUpdatesScript(now);
            return;
        }
        if (m_testKit == "integrity")
        {
            TestIntegrityScript(now);
            return;
        }
        GameSlot& slot = SlotFor(m_testKit == "giants" ? giants : ssa);
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL kit step %d (%s): %s (line \"%s\")\n", m_testStep, m_testKit.c_str(), what,
                    SlotStatus(&slot).text.c_str());
                m_testFailures++;
            }
        };
        const std::string tag = "v" + LauncherVersion();
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 120000)
        {
            fprintf(stderr, "kit script: install error \"%s\", kit error \"%s\"\n", InstallSnapshot(slot).error.c_str(),
                slot.kitError.c_str());
            expect(false, "timeout");
            m_running = false;
            return;
        }
        switch (m_testStep)
        {
        case 0: // the discs checked in the background
            m_page = Page::Play;
            if (m_testBegan == now)
            {
                SelectGame(giants);
                SelectGame(ssa);
                return;
            }
            if (SlotFor(ssa).disc == DiscState::Verifying || SlotFor(giants).disc == DiscState::Verifying)
                return;
            expect(slot.disc == DiscState::Ready, "the disc is ready");
            m_s = &slot;
            SetActiveGame(slot.game.id);
            RefreshInstall();
            expect(slot.kit && !slot.kit->chosen && slot.kit->tag.empty() && slot.kit->version == "old",
                "the kit next to the launcher is in use");
            expect(
                m_testKit == "old" ? KitOlderThanLauncher(*slot.kit, LauncherVersion()) : !slot.kit->Builds("giants"),
                "and it is not the one for this game");
            expect(KitNeedsFetch(slot), "so INSTALL fetches the right kit first");
            m_testStep++;
            break;
        case 1: // INSTALL: the download comes first, and the old kit never runs
            StartInstall();
            expect(slot.kitUsed && slot.kitDownload.Running() && !slot.installer.Running(),
                "the tools are downloaded before anything is built");
            expect(SlotStatus(&slot).text.rfind("Downloading tools", 0) == 0, "the sidebar says Downloading tools");
            m_testStep++;
            break;
        case 2:
            if (slot.installer.Running() || slot.kitDownload.Running() || slot.installView)
                return;
            expect(slot.kit && slot.kit->version == "kit-new" && slot.kit->tag == tag && slot.kit->Builds(slot.game.id),
                "the downloaded kit is the one in use");
            expect(slot.kit && FindInstalledGame(m_config, slot.check.xexSha256).kitVersion == "kit-new",
                "the game was built by it");
            expect(!slot.installed.runtime.empty(), "and is there");
            expect(SettingGet(m_config, "update", "kit_fetched") == tag, "the fetch is remembered");
            m_running = false;
            break;
        }
    }

    // The game updates (SKY_LAUNCHER_TEST_KIT=updates, SKY_LAUNCHER_BASE_PATH holding a kit that builds both games at SSA 0.9.0 and
    // Giants 0.8.0, SKY_LAUNCHER_UPDATE_URL a fake release whose notes bump Giants to 0.8.1 only, its kit being "kit-new"):
    // both games built from the kit at hand, then the check as made at every start: SSA stays PLAY, Giants shows UPDATE, nothing
    // is downloaded meanwhile, "Play without updating" gives PLAY back, UPDATE fetches the kit and rebuilds Giants only.
    // "offline" has no release to reach: the check fails without a word.
    void App::TestUpdatesScript(uint64_t now)
    {
        const GameInfo& ssa = *FindGame("ssa");
        const GameInfo& giants = *FindGame("giants");
        GameSlot& a = SlotFor(ssa);
        GameSlot& g = SlotFor(giants);
        const bool offline = m_testKit == "offline";
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL updates step %d (%s): %s (Giants \"%s\")\n", m_testStep, m_testKit.c_str(), what,
                    SlotStatus(&g).text.c_str());
                m_testFailures++;
            }
        };
        auto shoot = [&](const char* name, uint64_t wait = 500) {
            m_toasts.Clear(); // the install's "Game installed" would sit on the banner
            m_testShot = fmt::format("{:02}-{}", m_testStep, name);
            m_testAt = now + wait;
        };
        auto busy = [](GameSlot& slot) {
            return slot.installer.Running() || slot.kitDownload.Running() || slot.installView;
        };
        auto toolsLine = [&](GameSlot& slot) { return SlotStatus(&slot).text.rfind("Downloading tools", 0) == 0; };
        const std::string next = [] {
            SemVer v = ParseSemVer(LauncherVersion());
            return fmt::format("v{}.{}.{}", v.major, v.minor, v.patch + 1);
        }();
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 240000)
        {
            expect(false, "timeout");
            m_running = false;
            return;
        }
        switch (m_testStep)
        {
        case 0: // the discs checked in the background
            m_page = Page::Play;
            if (m_testBegan == now)
            {
                if (!offline)
                    SelectGame(giants);
                SelectGame(ssa);
                return;
            }
            if (a.disc == DiscState::Verifying || (!offline && g.disc == DiscState::Verifying))
                return;
            expect(a.disc == DiscState::Ready && (offline || g.disc == DiscState::Ready), "the discs are ready");
            m_testStep = offline ? 20 : 1;
            break;
        case 1: // both games built from the kit at hand (no download: it is not older than the launcher)
            SelectGame(ssa);
            StartInstall();
            expect(!a.kitUsed && a.installer.Running(), "the kit at hand builds Spyro's Adventure");
            m_testStep++;
            break;
        case 2:
            if (busy(a))
                return;
            expect(!a.installed.runtime.empty() && a.installed.gameVersion == "0.9.0",
                "Spyro's Adventure is built, at 0.9.0");
            SelectGame(giants);
            StartInstall();
            expect(!g.kitUsed && g.installer.Running(), "and Giants");
            m_testStep++;
            break;
        case 3:
            if (busy(g))
                return;
            expect(!g.installed.runtime.empty() && g.installed.gameVersion == "0.8.0" && GameVersion(g) == "0.8.0",
                "Giants is built, at 0.8.0");
            expect(m_gameUpdates.empty() && !UpdateAvailable(g) && SlotStatus(&g).button == std::string("PLAY"),
                "nothing is offered before a check");
            SelectGame(giants);
            shoot("giants-built");
            break;
        case 4: // the check made at every start
            StartUpdateCheck(false);
            m_testStep++;
            break;
        case 5:
            if (m_updates.Busy() || m_updateWasBusy)
                return;
            expect(m_gameUpdates.size() == 2, "the release's games line was read");
            expect(UpdateAvailable(g) && !UpdateAvailable(a), "Giants only has an update");
            expect(SlotStatus(&g).button == std::string("UPDATE") && SlotStatus(&a).button == std::string("PLAY"),
                "UPDATE for Giants, PLAY for the other");
            expect(!toolsLine(g) && !toolsLine(a) && !std::filesystem::exists(DownloadedKitsFolder()),
                "no kit is downloaded in the background");
            expect(m_updates.Snapshot().launcherNewer, "the launcher's own update is still offered");
            {
                GameSlot& unbuilt = SlotFor(*FindGame("swapforce"));
                m_gameUpdates.push_back({ unbuilt.game.id, "0.5.9", "v9.9.9" });
                expect(GameVersion(unbuilt) == "0.5.9", "a game not built and with no kit shows the release's version");
                m_gameUpdates.pop_back();
            }
            SelectGame(giants);
            shoot("giants-update");
            break;
        case 6: // "Play without updating"
            m_testPlayWithout = true;
            m_testStep++;
            break;
        case 7:
            expect(g.skipUpdate && !UpdateAvailable(g) && SlotStatus(&g).button == std::string("PLAY"),
                "Play without updating gives PLAY back");
            shoot("giants-play-without");
            break;
        case 8:
            g.skipUpdate = false;
            SelectGame(ssa);
            shoot("ssa-up-to-date");
            break;
        case 9: // UPDATE: the kit first, then the rebuild
            SelectGame(giants);
            expect(UpdateAvailable(g) && KitNeedsFetch(g), "UPDATE needs the release's kit");
            SDL_SetEnvironmentVariable(
                SDL_GetEnvironment(), "FAKE_ARKEYAN_BUILDER_DELAY", "0.15", true); // slow enough for a shot of the card
            StartInstall();
            expect(g.kitUsed && g.kitDownload.Running() && g.kitTag == next,
                "UPDATE fetches the kit of the release the game names");
            shoot("giants-updating", 1500);
            break;
        case 10:
            if (busy(g))
                return;
            expect(g.kit && g.kit->tag == next && SettingGet(m_config, "update", "kit_fetched") == next,
                "the new kit is in use");
            expect(g.installed.gameVersion == "0.8.1" && !UpdateAvailable(g) &&
                       SlotStatus(&g).button == std::string("PLAY"),
                "Giants is at 0.8.1 and PLAY again");
            expect(FindInstalledGame(m_config, a.check.xexSha256).kitVersion != "kit-new" &&
                       a.installed.gameVersion == "0.9.0",
                "Spyro's Adventure was not rebuilt");
            shoot("giants-updated");
            break;
        case 11: // "Update all" on Home: both games get an older build, the release names the versions the kit gives
        {
            for (GameSlot* slot : { &a, &g })
            {
                SelectGame(slot->game);
                RecordInstalledGame(
                    m_config, slot->check.xexSha256, slot->installed.runtime, slot->installed.kitVersion, "0.1.0");
                RefreshInstall();
            }
            m_gameUpdates = { { ssa.id, "0.9.0", next }, { giants.id, "0.8.1", next } };
            expect(GamesToUpdate() == 2 && !UpdatingAll(), "two games have an update");
            m_page = Page::Home;
            shoot("home-update-all");
            break;
        }
        case 12: // one click: Spyro's Adventure first, then Giants; Home stays shown
            StartUpdateAll();
            expect(UpdatingAll() && m_updateAll.total == 2 && m_updateAll.current == ssa.id && a.installer.Running(),
                "Spyro's Adventure starts first");
            expect(!g.installer.Running() && m_page == Page::Home, "the second waits, the page stays");
            shoot("home-updating-all", 700);
            break;
        case 13:
            if (UpdatingAll() || busy(a) || busy(g))
                return;
            expect(a.installed.gameVersion == "0.9.0" && g.installed.gameVersion == "0.8.1" && GamesToUpdate() == 0,
                "both games are updated, one after the other");
            expect(m_updateAll.started == 2 && m_updateAll.failed == 0 && m_page == Page::Home,
                "none failed, still on Home");
            shoot("home-updated-all");
            break;
        case 14: // Cancel stops the running one and the one waiting
            for (GameSlot* slot : { &a, &g })
            {
                SelectGame(slot->game);
                RecordInstalledGame(
                    m_config, slot->check.xexSha256, slot->installed.runtime, slot->installed.kitVersion, "0.1.0");
                RefreshInstall();
            }
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_ARKEYAN_BUILDER_DELAY", "0.5", true);
            StartUpdateAll();
            expect(UpdatingAll() && a.installer.Running(), "a second run starts");
            shoot("home-updating-again",
                1500); // the shot is taken once the first build has run a moment; the step moves on after it
            break;
        case 15:
            CancelUpdateAll();
            expect(m_updateAll.queue.empty(), "Cancel empties the queue");
            m_testStep++;
            break;
        case 16: // a cancelled build leaves its card (interrupted), which is not "busy": only the running jobs are waited for
            if (UpdatingAll() || a.installer.Running() || g.installer.Running() || a.kitDownload.Running() ||
                g.kitDownload.Running())
                return;
            expect(GamesToUpdate() == 2 && !g.installer.Running() && g.installed.gameVersion == "0.1.0",
                "cancelled: nothing more starts, both still old");
            shoot("home-cancelled");
            break;
        case 17: m_running = false; break;
        case 20: // offline
            StartUpdateCheck(false);
            m_testStep++;
            break;
        case 21:
            if (m_updates.Busy() || m_updateWasBusy)
                return;
            expect(!m_updates.Snapshot().checked && m_gameUpdates.empty() && BannerHeight() == 0 &&
                       m_toasts.Items().empty(),
                "offline: nothing is said");
            expect(!UpdateAvailable(a) && !std::filesystem::exists(DownloadedKitsFolder()),
                "and nothing is offered or downloaded");
            shoot("offline");
            break;
        default: m_running = false; break;
        }
    }

    // The sidebar's line through an install and an update (SKY_LAUNCHER_TEST_SIDEBAR, with a [launcher] kit= that is the fake
    // kit and both games' iso=): Spyro's Adventure ready, installing at 0, 50 and 100 %, Giants shown meanwhile (the other
    // game's line), ready, an update available, updating at 0, 50 and 100 %, then a build that fails. Each shot "<nn>-<state>"
    // is checked against the line the sidebar draws (m_testFailures).
    void App::TestSidebarScript(uint64_t now)
    {
        const GameInfo& ssa = *FindGame("ssa");
        const GameInfo& giants = *FindGame("giants");
        GameSlot& a = SlotFor(ssa);
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(
                    stderr, "FAIL sidebar step %d: %s (line \"%s\")\n", m_testStep, what, SlotStatus(&a).text.c_str());
                m_testFailures++;
            }
        };
        auto line = [&] { return SlotStatus(&a).text; };
        auto starts = [&](const char* prefix) { return line().rfind(prefix, 0) == 0; };
        auto env = [](const char* name, const char* value) {
            SDL_SetEnvironmentVariable(SDL_GetEnvironment(), name, value, true);
        };
        auto shoot = [&](const char* name, uint64_t wait = 300) {
            m_testShot = fmt::format("{:02}-{}", m_testStep, name);
            m_testAt = now + wait;
        };
        auto percent = [&] { return MakeInstallChecklist(InstallSnapshot(a), now).overall; };
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 300000)
        {
            expect(false, "timeout");
            m_running = false;
            return;
        }
        switch (m_testStep)
        {
        case 0: // both discs checked in the background
            m_page = Page::Play;
            if (m_testBegan == now)
            {
                SelectGame(giants);
                SelectGame(ssa);
                return;
            }
            if (a.disc == DiscState::Verifying || SlotFor(giants).disc == DiscState::Verifying)
                return;
            expect(a.disc == DiscState::Ready, "SSA's disc is ready");
            m_testSha = a.check.xexSha256;
            env("FAKE_ARKEYAN_BUILDER_DELAY", "0.08");
            env("FAKE_ARKEYAN_BUILDER_HOLD", "2");
            expect(line() == "Ready to install", "a verified disc with no build is not ready to play");
            shoot("before-install", 600);
            break;
        case 1: // INSTALL: the line follows the install from its first moment
            StartInstall();
            expect(starts("Installing") && SlotStatus(&a).progress >= 0 && !SlotStatus(&a).buttonEnabled &&
                       SlotStatus(&a).button == std::string("INSTALLING..."),
                "Installing at the start");
            shoot("installing-0", 20);
            break;
        case 2:
            if (percent() < 0.5f)
                return;
            expect(starts("Installing") || starts("Finishing"), "Installing at 50 %");
            shoot("installing-50", 20);
            break;
        case 3: // the other game is shown: SSA's line goes on in the sidebar
            SelectGame(giants);
            expect(m_s != &a && (starts("Installing") || starts("Finishing")), "SSA's line while Giants is shown");
            shoot("installing-other-game", 300);
            break;
        case 4: // back on SSA, held at the end of the last step
            SelectGame(ssa);
            if (percent() < 0.99f)
                return;
            expect(starts("Finishing") && SlotStatus(&a).progress >= 0.99f, "Finishing at 100 %");
            shoot("installing-100", 20);
            break;
        case 5:
            if (a.installer.Running() || a.installView)
                return;
            expect(!a.installed.runtime.empty() && line() == "Ready to play", "Ready to play once built");
            shoot("ready", 4000);
            break;
        case 6: // the release publishes a newer version of the game than the one built: an update
            m_config["install." + m_testSha]["version"] = "0.0.1";
            m_gameUpdates = { { a.game.id, "99.0.0", "v99.0.0" } };
            RefreshInstall();
            expect(line() == "Update available", "Update available");
            shoot("update-available", 400);
            break;
        case 7:
            StartInstall();
            expect(starts("Updating") && SlotStatus(&a).progress >= 0 &&
                       SlotStatus(&a).button == std::string("UPDATING..."),
                "Updating at the start");
            shoot("updating-0", 20);
            break;
        case 8:
            if (percent() < 0.5f)
                return;
            expect(starts("Updating") || starts("Finishing"), "Updating at 50 %");
            shoot("updating-50", 20);
            break;
        case 9:
            if (percent() < 0.99f)
                return;
            expect(starts("Finishing") && SlotStatus(&a).progress >= 0.99f, "Finishing at 100 % of an update");
            shoot("updating-100", 20);
            break;
        case 10:
            if (a.installer.Running() || a.installView)
                return;
            expect(line() == "Ready to play", "Ready to play after the update");
            env("FAKE_ARKEYAN_BUILDER_HOLD", "0");
            env("FAKE_ARKEYAN_BUILDER_FAIL", "compile");
            m_config["install." + m_testSha]["kit"] = "0.0.0-old";
            RefreshInstall();
            StartInstall();
            m_testStep++;
            break;
        case 11:
            if (a.installer.Running())
                return;
            expect(line() == "Update failed" && SlotStatus(&a).button == std::string("PLAY"),
                "Update failed, the build still there");
            shoot("update-failed", 400);
            break;
        default:
        {
            // Every other state is put on the slot by hand, two steps each: the Play page (the sidebar's line and the main
            // button are checked against what was drawn), then Home (the card's line).
            constexpr int First = 12, Cases = int(std::size(StateCases)), Toasts = First + 2 * Cases;
            if (m_testStep < Toasts)
            {
                int n = (m_testStep - First) / 2;
                const StateCase& state = StateCases[n];
                if ((m_testStep - First) % 2 == 0)
                {
                    if (n > 0)
                        expect(m_drawn["home:ssa"] == StateCases[n - 1].text, "Home's card says the same");
                    if (n == 0)
                        m_testBuilt = a.installed.runtime;
                    a.disc = DiscState::Ready;
                    a.check.status = GameSourceStatus::Ok;
                    a.installView = false;
                    a.verify.done = true;
                    a.installed.runtime = m_testBuilt;
                    a.installed.kitVersion = a.kit ? a.kit->version : "";
                    // The release publishes version 99 of the game: only the state that built an older one is offered it.
                    m_gameUpdates = { { a.game.id, "99.0.0", "v99.0.0" } };
                    a.installed.gameVersion = "99.0.0";
                    state.setup(a);
                    m_page = Page::Play;
                    expect(line() == state.text, state.name);
                    shoot(fmt::format("{}-play", state.name).c_str(), 400);
                }
                else
                {
                    expect(m_drawn["sidebar:ssa"] == state.text, "the sidebar draws the state");
                    expect(m_drawn["button"] == state.button, "the main button draws the state");
                    m_page = Page::Home;
                    shoot(fmt::format("{}-home", state.name).c_str(), 400);
                }
                break;
            }
            if (m_testStep == Toasts)
            {
                expect(m_drawn["home:ssa"] == StateCases[Cases - 1].text, "Home's card says the same");
                a.verify.done = true;
                m_page = Page::Play;
                Toast(Tr("Game installed: press PLAY"), ui::ToastKind::Success);
                Toast(Tr("Log copied"), ui::ToastKind::Success);
                Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
                Toast(Tf("Saves restored from {}", "2026-10-26 09:30"), ui::ToastKind::Success);
                shoot("toasts-stacked", 400);
                break;
            }
            if (m_testStep == Toasts + 1)
            {
                expect(m_toasts.Items().size() == 4 && m_toasts.Items()[2].kind == ui::ToastKind::Error,
                    "four messages, one an error");
                m_testAt = now + 8500; // the others have gone by themselves (a message stays 5 to 8 s: feedback.cpp)
                m_testShot = fmt::format("{:02}-toasts-error-kept", m_testStep);
                break;
            }
            if (m_testStep == Toasts + 2)
            {
                expect(m_toasts.Items().size() == 1 && m_toasts.Items()[0].kind == ui::ToastKind::Error,
                    "only the error is left (it stays until closed)");
                m_toasts.Clear();
            }
            SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_ARKEYAN_BUILDER_FAIL");
            m_running = false;
            break;
        }
        }
    }

    // Keyboard and pad navigation (SKY_LAUNCHER_TEST_NAV): keys go to ImGui as a player's would, a shot after each group.
    // Switching games in place: both games' discs are checked and their builds made (the fake kit) while the window
    // stays, the state of each is kept, and PLAY returns the shown game and its own build.
    void App::TestSwitchScript(uint64_t now)
    {
        const GameInfo& ssa = *FindGame("ssa");
        const GameInfo& giants = *FindGame("giants");
        GameSlot& a = SlotFor(ssa);
        GameSlot& g = SlotFor(giants);
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL switch step %d: %s\n", m_testStep, what);
                m_testFailures++;
            }
        };
        auto folderOf = [](const std::string& runtime) {
            return std::filesystem::path(runtime).parent_path().filename().string();
        };
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 300000)
        {
            expect(false, "timeout");
            m_running = false;
            return;
        }
        switch (m_testStep)
        {
        case 0: // Spyro's Adventure first: its disc is checked in the background
            if (m_testBegan == now)
            {
                SelectGame(ssa);
                expect(a.started && m_s == &a && a.disc == DiscState::Verifying, "SSA's check started");
                return;
            }
            if (a.disc == DiscState::Verifying)
                return;
            expect(a.disc == DiscState::Ready && !a.check.xexSha256.empty(), "SSA's disc is ready");
            m_testSha = a.check.xexSha256;
            m_testPath = a.discPath;
            m_testStep++;
            break;
        case 1: // Giants: the window stays, its content is Giants', SSA's state is untouched
            SelectGame(giants);
            expect(m_s == &g && g.disc == DiscState::Verifying, "Giants shown at once, its check running");
            expect(a.disc == DiscState::Ready && a.check.xexSha256 == m_testSha && a.discPath == m_testPath,
                "SSA's state kept");
            expect(
                SettingGet(m_config, "launcher", "game") == "giants" && ActiveGame() == "giants", "Giants remembered");
            m_testStep++;
            break;
        case 2:
            if (g.disc == DiscState::Verifying)
                return;
            expect(g.disc == DiscState::Ready && !g.check.xexSha256.empty() && g.check.xexSha256 != m_testSha,
                "Giants' disc is its own");
            expect(g.discPath != m_testPath &&
                       a.assets.Get(LauncherAssets::Role::Logo).path != g.assets.Get(LauncherAssets::Role::Logo).path,
                "each game keeps its own disc and art");
            expect(NeedsInstall(), "Giants needs its install");
            StartInstall(); // Giants' build, in the background
            expect(Installing(), "Giants installing");
            m_testStep++;
            break;
        case 3: // back to SSA while Giants installs: SSA is not installing, its state is as left
            SelectGame(ssa);
            expect(m_s == &a && a.disc == DiscState::Ready && a.check.xexSha256 == m_testSha, "SSA back, as it was");
            expect(!Installing() && AnyInstalling() && NeedsInstall(), "the install belongs to Giants");
            m_testStep++;
            break;
        case 4: // (the progress card goes once the poll has recorded the build)
            if (AnyInstalling() || g.installView)
                return;
            expect(!g.installed.runtime.empty() && a.installed.runtime.empty(), "Giants built, SSA not");
            m_testRuntime[1] = g.installed.runtime;
            StartInstall(); // SSA's build
            m_testStep++;
            break;
        case 5:
            if (AnyInstalling() || a.installView)
                return;
            expect(!a.installed.runtime.empty() && a.installed.runtime != g.installed.runtime,
                "SSA built, in its own place");
            expect(folderOf(a.installed.runtime) == datadir::GameFolderName("ssa") &&
                       folderOf(g.installed.runtime) == datadir::GameFolderName("giants"),
                "each build is in its game's folder");
            m_testRuntime[0] = a.installed.runtime;
            m_testStep++;
            break;
        case 6: // PLAY on each game: its disc, its build, its id
            SelectGame(ssa);
            expect(!NeedsInstall() && !Installing(), "SSA ready to play");
            PlayNow();
            expect(!m_running && &ChosenGame() == &ssa && ChosenRuntime() == m_testRuntime[0] && a.chosen == m_testPath,
                "PLAY on SSA runs SSA");
            m_running = true;
            SelectGame(giants);
            PlayNow();
            expect(
                !m_running && &ChosenGame() == &giants && ChosenRuntime() == m_testRuntime[1] && g.chosen == g.discPath,
                "PLAY on Giants runs Giants");
            expect(a.chosen == m_testPath && a.chosenRuntime == m_testRuntime[0], "SSA's choice kept");
            m_testStep++;
            break;
        }
    }

    // The X on every seat (players, item, adventure), pressed three ways: the mouse on the X, Delete on the focused seat, and the
    // pad's X on it. Each try puts the seats back, presses, and checks that this seat alone is empty. Four steps per try.
    void App::TestSeatXScript(uint64_t now)
    {
        constexpr int Modes = 3, StepsPerTry = 4;
        std::vector<std::string> seats = m_s->collection->TestSeats();
        if (m_testStep == 0)
        {
            m_page = Page::Collection;
            m_s->collection->Seed();
            m_testAt = now + 600;
            return;
        }
        if (m_testStep == 1) // the seeded seats, once the page has loaded them
        {
            m_testSeats = seats;
            if (std::count_if(seats.begin(), seats.end(), [](const std::string& seat) { return !seat.empty(); }) != 4)
            {
                fprintf(stderr, "FAIL seat X: the seeded collection has no figure on every seat\n");
                m_testFailures++;
                m_running = false;
            }
            m_testAt = now + 300;
            return;
        }
        int tries = int(m_testSeats.size()) * Modes, step = m_testStep - 2, seat = step / StepsPerTry / Modes,
            mode = step / StepsPerTry % Modes;
        if (step / StepsPerTry >= tries)
        {
            m_running = false;
            return;
        }
        static const char* const ModeNames[Modes] = { "mouse", "keyboard", "pad" };
        ImGuiIO& io = ImGui::GetIO();
        switch (step % StepsPerTry)
        {
        case 0: // every seat occupied again
            m_s->collection->TestSetSeats(m_testSeats);
            break;
        case 1: // the pointer on the X, or the focus on the seat
            if (mode == 0)
            {
                ImVec2 at = m_s->collection->TestCrossAt(size_t(seat));
                if (at.x < 0)
                {
                    fprintf(stderr, "FAIL seat X: seat %d has no X on screen\n", seat);
                    m_testFailures++;
                }
                io.AddMousePosEvent(at.x, at.y);
            }
            else
                m_s->collection->TestFocusSeat(size_t(seat));
            break;
        case 2: // the press
            if (mode == 0)
            {
                io.AddMouseButtonEvent(0, true);
                io.AddMouseButtonEvent(0, false);
            }
            else
            {
                ImGuiKey key = mode == 1 ? ImGuiKey_Delete : ImGuiKey_GamepadFaceLeft;
                io.AddKeyEvent(key, true);
                io.AddKeyEvent(key, false);
            }
            break;
        case 3: // only this seat is empty
        {
            bool ok = seats.size() == m_testSeats.size();
            for (size_t i = 0; ok && i < seats.size(); i++)
                ok = int(i) == seat ? seats[i].empty() : seats[i] == m_testSeats[i];
            if (!ok)
            {
                fprintf(stderr, "FAIL seat X: seat %d by %s did not take its figure off\n", seat, ModeNames[mode]);
                m_testFailures++;
            }
            else
                printf("seat X: seat %d by %s: taken off\n", seat, ModeNames[mode]);
            break;
        }
        }
        m_testAt = now + 500;
    }

    void App::TestNavScript(uint64_t now)
    {
        // A key is pressed and released, unless Hold (down only) or Release (up only) wraps it.
        constexpr int Hold = 0x10000, Release = 0x20000;
        static const std::vector<int> Steps[] = {
            {}, // 40: the game page
            { ImGuiKey_Escape, ImGuiKey_Escape }, // 41: B: Home; B again: the ring on the game's row in the sidebar
            { ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow,
                ImGuiKey_DownArrow, ImGuiKey_DownArrow }, // 42: down: past the five other games and Coming later
            { ImGuiKey_Space }, // 43: A: Collection, the ring on Add a figure
            { ImGuiKey_GamepadFaceUp }, // 44: Y: the search box takes the keyboard
            { ImGuiKey_Escape }, // 45: B: out of the search
            { ImGuiKey_UpArrow }, // 46: up: to the seats, above the bar
            { ImGuiKey_Space }, // 47: A: choosing for that seat (lit, the bar says so)
            { ImGuiKey_DownArrow }, // 48: down: the shelf's first row (the banner's buttons are off to the right)
            { ImGuiKey_Space }, // 49: A: it goes on the seat, the choosing ends
            { ImGuiKey_GamepadFaceLeft }, // 50: X: the focused card's sheet
            { ImGuiKey_GamepadFaceRight, ImGuiKey_GamepadFaceRight,
                ImGuiKey_GamepadFaceRight }, // 51: B, B: closed, back to Home; B: the game's row
            // (a frame late: in the next step, downs would move the grid)
            { ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow,
                ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_DownArrow, ImGuiKey_Space,
                ImGuiKey_GamepadR2 }, // 52: down to Settings, A, RT: Settings > Controls
            { ImGuiKey_Escape }, // 53: Escape: back to Home
        };
        // Where each step's keys must have led (checked when the next step begins): the sidebar's rows, the collection, the
        // settings; a step that went elsewhere would open what the next keys press (a file chooser: the run would wait).
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL nav step %d: %s\n", 39 + m_testStep, what);
                m_testFailures++;
            }
        };
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > 120000)
        {
            expect(false, "timeout");
            m_running = false;
            return;
        }
        switch (m_testStep)
        {
        case 2: expect(m_page == Page::Home, "B on the game page goes to Home"); break;
        case 4: expect(m_page == Page::Collection, "A on the sidebar's Collection row opens the collection"); break;
        case 12: expect(m_page == Page::Home, "B, B close the sheet and the collection"); break;
        case 13:
            expect(m_page == Page::Settings && m_tab == SettingsTab::Controls, "Settings, then RT: its Controls tab");
            break;
        case 14: expect(m_page == Page::Home, "Escape leaves Settings for Home"); break;
        default: break;
        }
        if (m_testStep >= int(std::size(Steps)))
        {
            m_running = false;
            return;
        }
        if (m_testStep == 0)
        {
            m_page = Page::Play;
            m_s->collection->Seed();
            if (m_s->disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            if (m_s->disc == DiscState::Ready || m_s->disc == DiscState::Failed || m_testDrop.empty())
                m_testAt = now + 500;
            return;
        }
        // ImGui's input queue spreads the events over frames, in order.
        ImGuiIO& io = ImGui::GetIO();
        bool holding = false;
        for (int step : Steps[m_testStep])
        {
            ImGuiKey key = ImGuiKey(step & 0xFFFF);
            if (!(step & Release))
                io.AddKeyEvent(key, true);
            if (!(step & Hold))
                io.AddKeyEvent(key, false);
            holding = holding || (step & Hold) != 0;
        }
        // A pressed key takes two frames of the queue, and an idle window draws few: 100 ms a key past five.
        m_testAt = now + (holding ? 1500 : 600) + 100 * uint64_t(std::max(0, int(Steps[m_testStep].size()) - 5));
    }

    // The action button's states (SKY_LAUNCHER_TEST_BUTTONS, with a [launcher] kit= that is the fake kit and both games'
    // iso=): for each game INSTALL, under the pointer, pressed, focused, the settings under the
    // pointer, disabled, installing, then PLAY and PLAY under the pointer; then each game in the smallest window, at 150 %
    // and at 200 %. Shots "<game>-<step>-<state>".
    void App::TestButtonsScript(uint64_t now)
    {
        static const char* const Games[] = { "ssa", "giants" };
        constexpr int PerGame = 9, Extras = 3;
        ImGuiIO& io = ImGui::GetIO();
        int game = m_testStep / PerGame, step = m_testStep % PerGame;
        if (m_testStep >= 2 * PerGame)
        {
            // The window's extremes, PLAY on each game.
            int extra = (m_testStep - 2 * PerGame) / 2;
            game = (m_testStep - 2 * PerGame) % 2;
            if (extra >= Extras)
            {
                m_running = false;
                return;
            }
            static const char* const Names[] = { "small", "scale150", "scale200" };
            static const float Scales[] = { 1, 1.5f, 2 };
            SelectGame(*FindGame(Games[game]));
            io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            m_testUiScale = m_uiScale = Scales[extra];
            float w = extra == 0 ? WindowMinW : WindowW, h = extra == 0 ? WindowMinH : WindowH;
            SDL_SetWindowSize(m_window, int(w * m_uiScale), int(h * m_uiScale));
            m_testShot = fmt::format("{}-{:02}-{}", Games[game], PerGame + extra, Names[extra]);
            m_testAt = now + 1500;
            return;
        }
        GameSlot& slot = SlotFor(*FindGame(Games[game]));
        auto shoot = [&](const char* name, uint64_t wait = 300) {
            m_testShot = fmt::format("{}-{:02}-{}", Games[game], step, name);
            m_testAt = now + wait;
        };
        auto pointAt = [&](ImVec2 at) { io.AddMousePosEvent(at.x, at.y); };
        auto pointAway = [&] { io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); };
        switch (step)
        {
        case 0:
            if (m_s != &slot || !slot.started)
                SelectGame(slot.game); // its remembered disc checked
            m_page = Page::Play;
            pointAway();
            if (slot.disc == DiscState::Ready && !Installing() && !slot.installView)
                shoot("install", 600);
            break;
        case 1:
            pointAt(m_testMainAt);
            shoot("hover");
            break;
        case 2:
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            shoot("pressed");
            break;
        case 3:
            // Released away from the button (no click), then the focus as the keyboard or the pad puts it.
            pointAway();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            m_focusTo = FocusTo::Page;
            m_focusShow = true;
            shoot("focused");
            break;
        case 4:
            ImGui::SetNavCursorVisible(false); // as the mouse hides it
            pointAt(m_testGearAt);
            shoot("settings-hover");
            break;
        case 5:
            pointAway();
            m_testButtonsOff = true;
            shoot("disabled");
            break;
        case 6:
            m_testButtonsOff = false;
            if (!Installing() && !slot.installView)
            {
                SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "FAKE_ARKEYAN_BUILDER_DELAY", "0.03", true);
                StartInstall();
            }
            else if (Installing() && MakeInstallChecklist(InstallSnapshot(), now).overall >= 0.4f)
                shoot("installing", 50);
            break;
        case 7:
            if (!Installing() && !slot.installView)
                shoot("play", 4000); // after the "installed" toast
            break;
        case 8:
            pointAt(m_testMainAt);
            shoot("play-hover");
            m_testAt = now + 300;
            break;
        }
    }

    // A disc given on Home (SKY_LAUNCHER_TEST_SCREENS=issues, no disc image: each check's result is set by hand): a file
    // that is no usable disc leaves the shown game on the disc it had, with one message; another game's disc goes to that
    // game. Then a key, mouse or Toy Box capture left pending ends when the page or the game changes.
    void App::TestHomeDiscChecks()
    {
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL home disc: %s\n", what);
                m_testFailures++;
            }
        };
        const GameInfo* ssa = FindEnabledGame("ssa", m_config);
        const GameInfo* giants = FindEnabledGame("giants", m_config);
        if (ssa == nullptr || giants == nullptr)
        {
            expect(false, "both games are enabled");
            return;
        }
        GameSlot& a = SlotFor(*ssa);
        GameSlot& g = SlotFor(*giants);
        GoTo(Page::Home);
        SelectGame(*giants);
        g.verify.Join();
        const DiscState before = g.disc;
        const std::string beforePath = g.discPath;
        auto given = [&](const char* file, GameSourceStatus status, const char* other) {
            m_homeDisc = datadir::Text(std::filesystem::temp_directory_path() / file);
            g.disc = DiscState::Failed; // as the check of that file ends
            g.discPath = m_homeDisc;
            g.check = {};
            g.check.status = status;
            g.check.otherGame = other;
        };

        // Not a disc the launcher can use: Giants is back on its own disc, and says so once.
        m_toasts.Clear();
        given("not-a-disc.iso", GameSourceStatus::NotXboxDisc, "");
        PollSlots();
        expect(m_homeDisc.empty() && m_s == &g, "the shown game stays");
        expect(
            g.discPath == beforePath && (g.disc == before || (!beforePath.empty() && g.disc == DiscState::Verifying)),
            "the shown game is back on its previous disc");
        PollSlots();
        expect(m_toasts.Items().size() == 1 && m_toasts.Items()[0].kind == ui::ToastKind::Error, "one message");
        m_toasts.Clear();

        // Another supported game's disc: it goes to that game, and Giants keeps its own.
        if (a.disc == DiscState::Verifying) // a busy game refuses the disc
        {
            a.verify.Join();
            a.disc = DiscState::None;
        }
        given("other-game.iso", GameSourceStatus::WrongGame, "ssa");
        const std::string other = m_homeDisc;
        PollSlots();
        expect(m_s == &a && m_page == Page::Play && (a.discPath == other || a.fromCopy), "the disc went to SSA");
        expect(g.discPath == beforePath, "Giants kept its disc");
        m_toasts.Clear();

        // Captures: each one pending, then the page changes, then the game.
        for (int change = 0; change < 2; change++)
        {
            m_capturing = &GameKeys(ssa->id)[0];
            m_mouseCapturing = &GameKeys(ssa->id)[0];
            m_toyboxCapturing = &ToyboxKeys()[0];
            m_toyboxPadCapturing = &ToyboxPads()[0];
            if (change == 0)
                GoTo(Page::GameSettings);
            else
                SelectGame(*giants);
            expect(m_capturing == nullptr && m_mouseCapturing == nullptr && m_toyboxCapturing == nullptr &&
                       m_toyboxPadCapturing == nullptr,
                change == 0 ? "a capture ends with the page" : "a capture ends with the game");
        }
        GoTo(Page::Settings);
    }

    // Screens of the screenshot test, each captured once it has settled.
    // The three model screens (docs/polish_audit.md): Home, the game page and its Keyboard tab, as a player finds them: Spyro's
    // Adventure's disc (SKY_LAUNCHER_TEST_DROP) verified and its game built (by hand: the runtime is not run), Giants opened
    // with no disc, Swap Force not opened yet. With a pad (SKY_LAUNCHER_TEST_SCREENS=pad) the focus is on each page's first
    // stop, its ring shown; with the mouse, no ring. SKY_LAUNCHER_LANGUAGE and SKY_LAUNCHER_TEST_SIZE set the rest;
    // SKY_LAUNCHER_TEST_SCALE=2 draws them at twice the density (a Retina screen's), the same layout.
    void App::TestScreensScript(uint64_t now)
    {
        if (m_testScreens ==
            4) // the portal test's two results and the game page's chip with an Xbox 360 portal (0.9.5)
        {
            GameSlot& ssa = SlotFor(*FindGame("ssa"));
            // The original Wii portal on Linux, which opens and does not answer; then an Xbox 360 portal that answers
            // beside a Wii portal the system won't open.
            static const char* const silent =
                "{\"kind\":\"hid\",\"vid\":\"1430\",\"pid\":\"0150\",\"product\":\"Spyro Porta\",\"opened\":true,"
                "\"status\":\"error: write failed (Protocol error)\",\"write\":\"\",\"methods\":{\"hid-write\":\"R: "
                "error: write failed "
                "(Protocol error); S: not tried\",\"usb-control\":\"R: error: no 'R' answer within 1 s; S: not "
                "tried\"}}\n{\"done\":true,\"count\":1}\n";
            static const char* const found =
                "{\"kind\":\"usb\",\"vid\":\"1430\",\"pid\":\"1f17\",\"product\":\"Xbox 360 "
                "portal\",\"opened\":true,\"status\":\"ok\","
                "\"id\":\"01 28\",\"model\":\"Spyro's Adventure portal (Xbox "
                "360)\",\"verdict\":\"works\",\"verdict_reason\":\"\"}\n"
                "{\"kind\":\"hid\",\"vid\":\"1430\",\"pid\":\"0150\",\"product\":\"Spyro Porta\",\"opened\":false,"
                "\"status\":\"error: cannot open (Permission denied)\"}\n{\"done\":true,\"count\":2}\n";
            static const char* const shots[] = { "portal-not-answering", "portal-found", "portal-chip" };
            switch (m_testStep)
            {
            case 0:
                if (ssa.disc == DiscState::None)
                {
                    SelectGame(*FindGame("ssa"));
                    if (!m_testDrop.empty())
                        StartVerify(m_testDrop);
                }
                [[fallthrough]];
            case 1:
                if (ssa.disc == DiscState::Verifying)
                    return;
                ssa.installed.runtime = "fixture"; // the Test button on (once the disc check is done: it sets the slot)
                m_page = Page::Settings;
                m_tab = SettingsTab::Portal;
                m_probe.result = ParsePortalProbe(m_testStep == 0 ? silent : found);
                m_probe.ran = true;
                m_probe.done = true;
                break;
            case 2:
                if (ssa.disc == DiscState::Verifying)
                    return;
                ssa.installed.runtime = "test-built";
                ssa.installed.gameVersion = "99.0.0";
                m_testPortal = launcher_portal::Status{ true, true, "Xbox 360 portal" };
                m_page = Page::Play;
                break;
            default: m_running = false; return;
            }
            m_testShot = shots[m_testStep];
            m_testAt = now + (m_testStep == 2 ? 2200 : 900);
            return;
        }
        if (m_testScreens == 3) // the changed Settings and Collection screens, without a release request or a game
        {
            m_page = Page::Settings;
            m_tab = SettingsTab::Controls;
            static const char* const shots[] = { "controls", "toybox-info", "update-one", "update-several", "support",
                "about", "edit", "unknown-type", "unknown-named", "missing-figure", "home-disc" };
            switch (m_testStep)
            {
            case 0: break;
            case 1: // the Toy Box row: its hint is a line under its label (no info icon)
                m_tab = SettingsTab::Portal;
                break;
            case 2:
            case 3:
            {
                m_tab = SettingsTab::General;
                GameSlot& giants = SlotFor(*FindGame("giants"));
                giants.disc = DiscState::Ready;
                giants.installed.runtime = "fixture";
                giants.installed.gameVersion = "0.0.1";
                m_gameUpdates = { { "giants", "1.0.0", "v1.0.0" } };
                if (m_testStep == 3)
                {
                    GameSlot& ssa = SlotFor(*FindGame("ssa"));
                    ssa.disc = DiscState::Ready;
                    ssa.installed.runtime = "fixture";
                    ssa.installed.gameVersion = "0.0.1";
                    m_gameUpdates.push_back({ "ssa", "1.0.0", "v1.0.0" });
                }
                m_updateDoneNote = GameUpdateNote();
                const std::string expected =
                    m_testStep == 2
                        ? Tf("Arkeyan Launcher is up to date. An update is available for {}.", "Skylanders: Giants")
                        : Tf("Arkeyan Launcher is up to date. {} game updates available.", 2);
                if (m_updateDoneNote != expected)
                {
                    fprintf(stderr, "FAIL update summary: wrong game or count\n");
                    m_testFailures++;
                }
                break;
            }
            case 4: m_tab = SettingsTab::Help; break;
            case 5:
                m_tab = SettingsTab::Help;
                m_testScrollEnd = true; // About ends the tab
                break;
            case 6:
            case 7:
            case 8:
                m_page = Page::Collection;
                m_s->collection->Seed();
                m_s->collection->TestScreen(m_testStep == 6 ? 7 : m_testStep == 7 ? 46 : 47);
                break;
            case 9:
                m_page = Page::Collection;
                m_s->collection->TestScreen(48);
                break;
            case 10:
                m_s->collection->TestScreen(49);
                TestHomeDiscChecks();
                break;
            default: m_running = false; return;
            }
            m_testShot = std::string("issues-") + shots[m_testStep];
            m_testAt = now + 900;
            return;
        }
        const bool pad = m_testScreens == 2;
        auto shoot = [&](const char* name, Page page, uint64_t wait) {
            m_page = page;
            ShowGameTab(GameTab::Controls, GameSection::Keyboard);
            m_focusTo = FocusTo::Page;
            m_focusShow = pad;
            ImGui::SetNavCursorVisible(pad);
            m_testShot = fmt::format("screen-{}", name);
            m_testAt = now + wait;
        };
        GameSlot& ssa = SlotFor(*FindGame("ssa"));
        switch (m_testStep)
        {
        case 0:
            if (const char* scale = LauncherEnv("SKY_LAUNCHER_TEST_SCALE"); scale != nullptr && std::atof(scale) > 0)
            {
                int w = int(WindowW), h = int(WindowH);
                if (const char* size = LauncherEnv("SKY_LAUNCHER_TEST_SIZE"))
                    std::sscanf(size, "%dx%d", &w, &h);
                m_testUiScale = m_uiScale = float(std::atof(scale));
                SDL_SetWindowSize(m_window, int(float(w) * m_uiScale), int(float(h) * m_uiScale));
            }
            SelectGame(*FindGame("giants"));
            SelectGame(*FindGame("ssa"));
            if (ssa.disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            m_page = Page::Play;
            m_testStep = 1;
            return;
        case 1:
            if (ssa.disc == DiscState::Verifying)
                return;
            ssa.installed.runtime = "test-built";
            ssa.installed.gameVersion = "99.0.0";
            shoot("home", Page::Home, 600);
            break;
        case 2: shoot("game", Page::Play, 2200); break; // the logo has fallen
        case 3: shoot("keys", Page::GameSettings, 600); break;
        default: m_running = false; break;
        }
    }

    // One game at a time (SKY_LAUNCHER_TEST_RUNNING, with SKY_LAUNCHER_RUNTIME a stand-in game that runs a few seconds and exits
    // with 3, and SKY_LAUNCHER_TEST_DROP Spyro's Adventure's disc): PLAY starts it and the window stays; its button says Running,
    // a second PLAY starts nothing, another game's button waits for it; once it stops (a crash: its exit code is told) PLAY is
    // back. Shots: running-1-game, running-2-other, running-3-ended.
    void App::TestRunningScript(uint64_t now)
    {
        GameSlot& ssa = SlotFor(*FindGame("ssa"));
        GameSlot& giants = SlotFor(*FindGame("giants"));
        auto expect = [&](bool ok, const char* what) {
            if (!ok)
            {
                fprintf(stderr, "FAIL running step %d: %s (running \"%s\")\n", m_testStep, what,
                    m_session.Running().c_str());
                m_testFailures++;
            }
        };
        auto shoot = [&](const char* name) {
            m_page = Page::Play;
            m_testShot = fmt::format("running-{}", name);
            m_testAt = now + 600;
        };
        if (const char* mode = LauncherEnv("SKY_LAUNCHER_TEST_RUNNING");
            mode != nullptr && std::string_view(mode) == "controls")
        {
            switch (m_testStep)
            {
            case 0:
                m_startGames = true;
                SelectGame(*FindGame("giants"));
                SelectGame(*FindGame("ssa"));
                ssa.disc = DiscState::Ready;
                ssa.discPath = "--fake-game";
                ssa.installed.runtime = LauncherEnv("SKY_LAUNCHER_RUNTIME");
                ssa.installed.gameVersion = "99.0.0";
                m_testBegan = now;
                {
                    std::string error;
                    m_session.Start("ssa", { ssa.installed.runtime, {}, {} }, "--fake-game", error);
                }
                expect(m_session.Ours() && m_session.Running() == "ssa", "the fake game starts");
                shoot("control-started");
                return;
            case 1:
                if (now - m_testBegan > 10000)
                {
                    expect(false, "the fake game's ready file appears");
                    m_running = false;
                    return;
                }
                if (!std::filesystem::exists(LauncherEnv("SKY_FAKE_GAME_READY")))
                    return;
                if (now - m_testBegan < 1400)
                    return;
                expect(m_drawn["sidebar:ssa"] == "Running" && m_drawn["hero-main"] == "Stop",
                    "the sidebar runs and STOP is enabled");
                expect(m_drawn["hero-show"] == "hidden", "a windowless process has no Show button");
                expect(m_drawn["hero-gear"] == "hidden" && m_drawn["hero-pickers"] == "hidden",
                    "running has no settings or device pickers");
                expect(m_drawn["hero-status"].starts_with("Playing now · ") &&
                           m_drawn["hero-status"] != Tf("Playing now · {} this session", "0:00"),
                    "session time advances from the process start");
                expect(m_session.Stop(now), "STOP requests the owned process's end");
                shoot("control-stopping");
                return;
            case 2:
                if (m_session.Ours() && now - m_testBegan < 15000)
                {
                    expect(m_drawn["hero-main"] == "Stop (off)" && m_drawn["hero-status"] == Tr("Stopping..."),
                        "STOP is disabled while stopping");
                    return;
                }
                expect(m_session.Running().empty(), "the fake game stops");
                expect(m_drawn["sidebar:ssa"] == "Ready to play" && m_drawn["hero-main"] == "PLAY",
                    "the page and sidebar return to ready");
                expect(SlotStatus(&giants).buttonEnabled, "other games can start again");
                expect(!m_drawn.count("stopped"), "a requested stop has no crash dialog");
                shoot("control-ready");
                return;
            default: m_running = false; return;
            }
        }
        switch (m_testStep)
        {
        case 0:
            m_startGames = true;
            SelectGame(*FindGame("giants"));
            SelectGame(*FindGame("ssa"));
            if (ssa.disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            m_page = Page::Play;
            m_testBegan = now;
            m_testStep = 1;
            return;
        case 1:
        {
            if (ssa.disc == DiscState::Verifying)
                return;
            expect(ssa.disc == DiscState::Ready, "the disc is checked");
            ssa.installed.runtime = "test-built"; // built by hand: SKY_LAUNCHER_RUNTIME is what runs
            PlayNow();
            PlayNow(); // a double click: still one game
            const SlotLine line = SlotStatus(&ssa);
            expect(m_session.Running() == "ssa" && m_session.Ours(), "PLAY starts the game, the window stays");
            expect(line.state == SlotState::Running && line.button == "Running" && !line.buttonEnabled &&
                       line.text == "Running",
                "its button says Running, off");
            expect(!m_toasts.Items().empty() && m_toasts.Items().back().text == "Spyro's Adventure is running",
                "the second PLAY is refused");
            m_toasts.Clear();
            shoot("1-game");
            return;
        }
        case 2:
        {
            SelectGame(*FindGame("giants"));
            const SlotLine line = SlotStatus(&giants);
            expect(line.button == "Spyro's Adventure is running" && !line.buttonEnabled, "another game waits for it");
            shoot("2-other");
            return;
        }
        case 3:
            if (m_session.Ours())
            {
                if (now - m_testBegan > 30000)
                {
                    expect(false, "the stand-in game stops");
                    m_running = false;
                }
                return;
            }
            expect(m_session.Running().empty(), "nothing runs once it stopped");
            expect(m_drawn["stopped"] == "ssa 3", "its crash is told");
            SelectGame(*FindGame("ssa"));
            expect(SlotStatus(&ssa).button == "PLAY" && SlotStatus(&ssa).buttonEnabled &&
                       SlotStatus(&giants).buttonEnabled,
                "PLAY is back");
            shoot("3-ended");
            return;
        default: m_running = false; return;
        }
    }

    void App::TestScript()
    {
        if (m_shots.empty() || m_testAt != 0)
            return;
        uint64_t now = SDL_GetTicks();
        if (m_testPadWalk)
        {
            TestPadWalkScript(now);
            return;
        }
        if (m_testProfiles)
        {
            TestProfilesScript(now);
            return;
        }
        if (m_testExperimental)
        {
            // A later game (SKY_LAUNCHER_TEST_EXPERIMENTAL=<id>, "1": SuperChargers): Home, its page with its disc
            // (SKY_LAUNCHER_TEST_DROP) verified first, in the game's own look, then Home again with its card.
            const GameInfo* game = FindGame(m_testExperimentalGame);
            uint64_t settle = 400;
            switch (game != nullptr ? m_testStep : 99)
            {
            case 0:
                m_page = Page::Home;
                m_testShot = m_testExperimentalGame + "-01-home";
                break;
            case 1:
                SelectGame(*game);
                m_page = Page::Play;
                if (!m_testDrop.empty())
                {
                    if (m_s->disc == DiscState::None)
                        StartVerify(m_testDrop);
                    if (m_s->disc != DiscState::Ready && m_s->disc != DiscState::Failed)
                        return; // asked again next frame
                    settle = 1500; // the art loads after the check
                }
                m_testShot = m_testExperimentalGame + "-02-page";
                break;
            case 2:
                m_page = Page::Home;
                m_testShot = m_testExperimentalGame + "-03-home";
                break;
            default: m_running = false; return;
            }
            m_testAt = now + settle;
            return;
        }
        if (m_testHdTex)
        {
            TestHdTexturesScript(now);
            return;
        }
        if (m_testHero)
        {
            TestHeroScript(now);
            return;
        }
        if (m_testInstall)
        {
            TestInstallScript(now);
            return;
        }
        if (m_testUpdate)
        {
            TestUpdateScript(now);
            return;
        }
        if (m_testUninstall)
        {
            TestUninstallScript(now);
            return;
        }
        if (m_testScreens != 0)
        {
            TestScreensScript(now);
            return;
        }
        if (m_testRunning)
        {
            TestRunningScript(now);
            return;
        }
        if (m_testPad)
        {
            // Each game's Controller tab (SKY_LAUNCHER_TEST_PAD): with a few buttons given other jobs, to see the rows; Swap Force's
            // Manage tab has the Nightmare option (a second shot).
            static const char* const Games[] = { "ssa", "giants", "swapforce" };
            if (m_testStep >= int(std::size(Games)) + 1)
            {
                m_running = false;
                return;
            }
            if (m_testStep < int(std::size(Games)))
            {
                SelectGame(*FindGame(Games[m_testStep]));
                SetPadTarget(m_config, Games[m_testStep], 5, 1); // RB does B's job
                SetPadTarget(m_config, Games[m_testStep], 6, -1); // LT is off
                m_config[GameInputSection(Games[m_testStep])]["rumble_p2"] = "low";
                m_page = Page::GameSettings;
                ShowGameTab(GameTab::Controls, GameSection::Controller);
                m_testShot = fmt::format("pad-{}", Games[m_testStep]);
            }
            else
            {
                SelectGame(*FindGame("swapforce"));
                m_config["swapforce"]["nightmare_from_start"] = "1";
                m_page = Page::GameSettings;
                ShowGameTab(GameTab::Manage);
                m_testShot = "game-swapforce";
            }
            m_testAt = now + 600;
            return;
        }
        if (!m_testGameTabs.empty())
        {
            // Each tab of one game's settings, FSR 3 and frame generation on so their rows show (set SKY_LAUNCHER_LANGUAGE for the
            // language, SKY_LAUNCHER_TEST_SIZE=<w>x<h> a window tall enough for the longest tab). Shots "tabs-<game>-<NN>-<tab>".
            struct TabShot
            {
                GameTab tab;
                GameSection section;
                const char* name;
            };
            static const TabShot Tabs[] = { { GameTab::Picture, GameSection::None, "picture" },
                { GameTab::Controls, GameSection::Keyboard, "keyboard" },
                { GameTab::Controls, GameSection::Controller, "controller" },
                { GameTab::Sound, GameSection::None, "sound" }, { GameTab::Manage, GameSection::None, "manage" },
                { GameTab::Manage, GameSection::Disc, "disc" }, { GameTab::Manage, GameSection::Saves, "saves" },
                { GameTab::Manage, GameSection::Repair, "repair" } };
            if (m_testStep == 0)
                if (const char* size = LauncherEnv("SKY_LAUNCHER_TEST_SIZE"))
                {
                    int w = 0, h = 0;
                    if (std::sscanf(size, "%dx%d", &w, &h) == 2)
                        SDL_SetWindowSize(m_window, w, h);
                }
            const GameInfo* game = FindGame(m_testGameTabs.c_str());
            if (game == nullptr || m_testStep >= int(std::size(Tabs)))
            {
                m_running = false;
                return;
            }
            SelectGame(*game);
            const std::string video = GameVideoSection(m_testGameTabs);
            m_config[video]["upscaler"] = "fsr3";
            m_config[video]["fsr_frame_generation"] = "on";
            m_page = Page::GameSettings;
            ShowGameTab(Tabs[m_testStep].tab, Tabs[m_testStep].section);
            m_testShot = fmt::format("tabs-{}-{:02}-{}", m_testGameTabs, m_testStep, Tabs[m_testStep].name);
            m_testAt = now + 600;
            return;
        }
        if (m_testKeys)
        {
            // Each game's Keyboard tab (set SKY_LAUNCHER_LANGUAGE for the language of the shots; SKY_LAUNCHER_TEST_SIZE=<w>x<h> a
            // taller window to see the Mouse section), then Settings > Controls and its Toy Box shortcuts.
            static const char* const Games[] = { "ssa", "giants", "swapforce", "trapteam", "superchargers",
                "imaginators" };
            if (m_testStep == 0)
                if (const char* size = LauncherEnv("SKY_LAUNCHER_TEST_SIZE"))
                {
                    int w = 0, h = 0;
                    if (std::sscanf(size, "%dx%d", &w, &h) == 2)
                        SDL_SetWindowSize(m_window, w, h);
                }
            if (m_testStep == int(std::size(Games)))
            {
                m_page = Page::Settings;
                m_tab = SettingsTab::Controls;
                m_toyboxShortcutsOpen = true;
                m_testShot = "keys-toybox";
                m_testAt = now + 600;
                return;
            }
            if (m_testStep > int(std::size(Games)))
            {
                m_running = false;
                return;
            }
            SelectGame(*FindGame(Games[m_testStep]));
            m_page = Page::GameSettings;
            ShowGameTab(GameTab::Controls, GameSection::Keyboard);
            m_testShot = fmt::format("keys-{}", Games[m_testStep]);
            m_testAt = now + 600;
            return;
        }
        if (m_testChests)
        {
            TestChestsScript(now);
            return;
        }
        if (m_testAchievements)
        {
            // The shown game's page with its disc verified and some progress, then its achievements (SKY_LAUNCHER_TEST_DROP names the disc;
            // SKYLANDERS_DATA a scratch folder: the progress is written there).
            switch (m_testStep)
            {
            case 0:
                m_page = Page::Play;
                m_testShot = "achievements-0-checking";
                m_testAt = now + 300;
                break;
            case 1:
                if (m_s->disc == DiscState::None && !m_testDrop.empty())
                    StartVerify(m_testDrop);
                if (m_s->disc == DiscState::Ready || m_s->disc == DiscState::Failed || m_testDrop.empty())
                {
                    // Six unlocks on different days, 12 h 34 of play, last played a moment ago.
                    achievements::List list = achievements::ReadCache(AchievementCacheFolder(m_s->game.id), "en");
                    const int64_t nowTime = int64_t(std::time(nullptr));
                    for (size_t i = 0; i < list.items.size() && i < 6; i++)
                        progress::WriteUnlock(progress::Folder(m_s->game.id), list.items[i * 2 % list.items.size()].id,
                            nowTime - int64_t(i) * 86400 * 3 - 600);
                    progress::WritePlayTime(
                        progress::Folder(m_s->game.id), { 12 * 3600 + 34 * 60 + 20, nowTime - 90, 9 });
                    RefreshProgress(true);
                    m_testShot = "achievements-1-game";
                    m_testAt = now + 1500;
                }
                break;
            case 2:
                GoTo(Page::Achievements);
                m_testShot = "achievements-2-list";
                m_testAt = now + 900;
                break;
            case 3:
                m_testShot = "achievements-3-end";
                m_testAt = now + 700;
                break;
            default: m_running = false;
            }
            return;
        }
        if (m_testLogo)
        {
            // Each game's page, once its logo has settled (SKY_LAUNCHER_TEST_SIZE sets the window).
            static const char* const Games[] = { "ssa", "giants", "swapforce" };
            if (m_testStep >= int(std::size(Games)))
            {
                m_running = false;
                return;
            }
            SelectGame(*FindGame(Games[m_testStep]));
            m_page = Page::Play;
            m_testShot = fmt::format("logo-{}", Games[m_testStep]);
            m_testAt = now + 2200;
            return;
        }
        if (m_testButtons)
        {
            TestButtonsScript(now);
            return;
        }
        if (m_testGallery)
        {
            TestGalleryScript(now);
            return;
        }
        if (m_testNav)
        {
            TestNavScript(now);
            return;
        }
        if (m_testSidebar)
        {
            TestSidebarScript(now);
            return;
        }
        if (!m_testKit.empty())
        {
            TestKitScript(now);
            return;
        }
        if (m_testSwitch)
        {
            TestSwitchScript(now);
            return;
        }
        if (m_testCopy)
        {
            TestCopyScript(now);
            return;
        }
        if (m_testSeatX)
        {
            TestSeatXScript(now);
            return;
        }
        if (m_testCollection)
        {
            // The collection's screens, in the scratch collection SKY_COLLECTION names.
            if (m_testStep >= CollectionPage::TestShotCount())
                m_running = false;
            else
            {
                if (m_testStep == 0)
                    m_s->collection->Seed();
                m_page = Page::Collection;
                m_s->collection->TestScreen(m_testStep);
                m_testAt = now + 700;
            }
            return;
        }
        switch (m_testStep)
        {
        case 0:
            m_page = Page::Play;
            m_testAt = now + 600;
            break;
        case 1:
            if (m_s->disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            if (m_s->disc == DiscState::Ready || m_s->disc == DiscState::Failed || m_testDrop.empty())
                m_testAt = now + 400;
            break;
        case 2:
            m_page = Page::GameSettings;
            ShowGameTab(GameTab::Picture);
            m_testAt = now + 400;
            break;
        case 3:
            m_page = Page::Settings;
            m_tab = SettingsTab::Controls;
            m_testAt = now + 400;
            break;
        case 4:
            // A few figures to show, in the test's own collection: characters, a legendary, a sidekick, an item and an
            // adventure pack; some sit on the next session's seats.
            m_s->collection->Seed();
            m_page = Page::Collection;
            m_s->collection->TestScreen(0);
            m_testAt = now + 600;
            break;
        case 5:
            m_page = Page::Home;
            m_testAt = now + 400;
            break;
        case 6:
            m_page = Page::Collection;
            m_s->collection->TestScreen(3);
            m_testAt = now + 600;
            break;
        case 7:
            m_s->collection->TestScreen(6);
            m_testAt = now + 600;
            break;
        case 8:
            m_page = Page::Play;
            m_testControllers = true;
            m_testAt = now + 500;
            break;
        case 9:
            m_page = Page::GameSettings;
            ShowGameTab(GameTab::Sound);
            m_testAt = now + 400;
            break;
        case 10:
            ShowGameTab(GameTab::Manage, GameSection::Disc);
            m_testAt = now + 400;
            break;
        case 11:
            ShowGameTab(GameTab::Manage, GameSection::Saves);
            m_testAt = now + 400;
            break;
        case 12:
            ShowGameTab(GameTab::Manage, GameSection::Repair);
            m_testAt = now + 400;
            break;
        case 13:
            m_page = Page::Settings;
            m_tab = SettingsTab::General;
            m_testAt = now + 400;
            break;
        case 14:
            // The verified image moved away: INSTALL says so instead of failing later.
            m_page = Page::Play;
            if (m_s->disc == DiscState::Ready)
            {
                m_s->discPath += ".moved";
                StartInstall();
            }
            m_testAt = now + 400;
            break;
        case 15:
            // The logo falling in, early in its fall (it eases out: most of the way is done in the first third): the disc put
            // back, the page entered again.
            if (m_s->discPath.ends_with(".moved"))
            {
                m_s->discPath.resize(m_s->discPath.size() - 6);
                m_s->disc = DiscState::Ready;
                m_s->check.status = GameSourceStatus::Ok;
            }
            m_page = Page::Play;
            m_lastPage = Page::Home;
            m_testAt = now + uint64_t(LogoDropMs / 8);
            break;
        case 16: // the launcher's Help tab, its About, and the problem report over it
            m_page = Page::Settings;
            m_tab = SettingsTab::Help;
            m_testAt = now + 400;
            break;
        case 17:
            m_tab = SettingsTab::Help;
            m_testScrollEnd = true;
            m_testAt = now + 400;
            break;
        case 18:
            m_testScrollEnd = false;
            m_tab = SettingsTab::Help;
            OpenProblemReport();
            m_testAt = now + 500;
            break;
        default: m_running = false; break;
        }
    }
}
