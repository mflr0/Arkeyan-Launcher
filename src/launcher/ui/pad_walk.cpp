// The pad walk (SKY_LAUNCHER_TEST_PADWALK): every page, tab and dialog shot as a pad player sees it, then walked with the
// D-pad alone (every control must be reached, every move in reading order; "quick": until every control is reached), then B
// (it must go back one step) and, on the main pages, LB and RB (the page before and after). docs/launcher_ui.md, "Navigation
// model". Scratch folders only, as every screenshot test.
#include "launcher/env.h"
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/collection_page.h"
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <fmt/format.h>
#include <imgui_internal.h>
#include <string_view>

namespace launcher_ui
{
    namespace
    {
        enum class BackTo { Sidebar, Home, GamePage, Closes, StaysOpen, Collection };
        struct WalkView
        {
            const char* name;
            BackTo back;
            bool mainPage; // LB / RB checked here
        };
        // The order of the sidebar: Home, the games, Collection, Settings (LB / RB step through it).
        struct MainPage
        {
            Page page;
            std::string game;
        };

        const char* BackName(BackTo back)
        {
            switch (back)
            {
            case BackTo::Sidebar: return "to the sidebar";
            case BackTo::Home: return "to Home";
            case BackTo::GamePage: return "to the game page";
            case BackTo::Closes: return "closes the dialog";
            case BackTo::StaysOpen: return "one step back inside the dialog";
            case BackTo::Collection: return "stays on the collection, one step back";
            }
            return "";
        }

        using Stop = ui::Context::TestStop;
        float MidX(const Stop& s) { return (s.min.x + s.max.x) / 2; }
        float MidY(const Stop& s) { return (s.min.y + s.max.y) / 2; }
        float OverlapX(const Stop& a, const Stop& b) { return std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x); }
        bool SameRow(const Stop& a, const Stop& b)
        {
            return std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y) > 0.5f * std::min(a.max.y - a.min.y, b.max.y - b.min.y);
        }

        // What a reader expects of a move (0 down, 1 right, 2 up, 3 left), from the stops drawn once it was made: empty when the
        // move is in reading order. The expected stop is the nearest one that way in the source's column (up, down) or row
        // (left, right); with none, any move is fine.
        std::string Disorder(const std::vector<Stop>& stops, uint32_t from, uint32_t to, int direction, const std::map<uint32_t, std::string>& labels)
        {
            auto find = [&](uint32_t id) -> const Stop* {
                for (const Stop& stop : stops)
                    if (stop.id == id)
                        return &stop;
                return nullptr;
            };
            auto name = [&](uint32_t id) {
                const auto label = labels.find(id);
                std::string text = label != labels.end() ? label->second : fmt::format("{:08x}", id);
                for (const Stop& stop : stops)
                    if (stop.id == id)
                        text += fmt::format(" ({:.0f},{:.0f})", stop.min.x, stop.min.y);
                return text;
            };
            const Stop* a = find(from);
            if (a == nullptr)
                return {};
            const bool vertical = direction == 0 || direction == 2;
            auto beyond = [&](const Stop& c) {
                switch (direction)
                {
                case 0: return !SameRow(c, *a) && MidY(c) > MidY(*a);
                case 2: return !SameRow(c, *a) && MidY(c) < MidY(*a);
                case 1: return MidX(c) > MidX(*a) && OverlapX(c, *a) <= 0;
                default: return MidX(c) < MidX(*a) && OverlapX(c, *a) <= 0;
                }
            };
            auto distance = [&](const Stop& c) { return vertical ? std::abs(MidY(c) - MidY(*a)) : std::abs(MidX(c) - MidX(*a)); };
            const Stop* expected = nullptr;
            for (const Stop& c : stops)
            {
                // A stop scrolled out of its list is never the expected one (a list scrolls on, a move from outside skips it).
                if (c.root != a->root || c.id == from || !c.shown || !beyond(c) || (vertical ? OverlapX(c, *a) <= 0 : !SameRow(c, *a)))
                    continue;
                if (expected == nullptr || distance(c) < distance(*expected))
                    expected = &c;
            }
            if (expected == nullptr)
                return {};
            static const char* const Names[] = { "down", "right", "up", "left" };
            const Stop* b = find(to);
            if (to == from)
                return fmt::format("{} from {} stays, {} is that way", Names[direction], name(from), name(expected->id));
            if (b == nullptr)
                return {}; // a stop the walk doesn't know (0.8.0's selects and fields were not recorded)
            if (!beyond(*b))
                return fmt::format("{} from {} goes the wrong way, to {}", Names[direction], name(from), name(to));
            if (!b->shown)
                return {}; // a list scrolling on to its next row, before the focus leaves it (the model's rule)
            if (vertical ? !SameRow(*b, *expected) && distance(*b) > distance(*expected) : !SameRow(*b, *a) || distance(*b) > distance(*expected) + 1)
                return fmt::format("{} from {} goes to {}, past {}", Names[direction], name(from), name(to), name(expected->id));
            return {};
        }

        uint32_t RootOf(ImGuiWindow* window) { return window != nullptr && window->RootWindow != nullptr ? window->RootWindow->ID : 0; }
        const char* RootName(ImGuiWindow* window) { return window != nullptr && window->RootWindow != nullptr ? window->RootWindow->Name : "(none)"; }
    }

    void App::TestPadWalkScript(uint64_t now)
    {
        static const WalkView Views[] = {
            { "00-first-start", BackTo::Sidebar, false },
            { "01-home", BackTo::Sidebar, true },
            { "02-game-ssa", BackTo::Home, true },
            { "03-game-giants", BackTo::Home, true },
            { "04-game-swapforce", BackTo::Home, true },
            { "05-play-with-menu", BackTo::Closes, false },
            { "06-ssa-video", BackTo::GamePage, false },
            { "07-ssa-keyboard", BackTo::GamePage, false },
            { "08-ssa-controller", BackTo::GamePage, false },
            { "09-ssa-language", BackTo::GamePage, false },
            { "10-ssa-disc", BackTo::GamePage, false },
            { "11-ssa-saves", BackTo::GamePage, false },
            { "12-ssa-game", BackTo::GamePage, false },
            { "13-portable-export", BackTo::Closes, false },
            { "14-giants-keyboard", BackTo::GamePage, false },
            { "15-giants-controller", BackTo::GamePage, false },
            { "16-swapforce-keyboard", BackTo::GamePage, false },
            { "17-swapforce-game", BackTo::GamePage, false },
            { "18-achievements", BackTo::GamePage, false },
            { "19-settings-controls", BackTo::Home, true },
            { "20-settings-launcher", BackTo::Home, false },
            { "21-uninstall-list", BackTo::Collection /* checked by hand below */, false },
            { "22-settings-support", BackTo::Home, false },
            { "23-problem-report", BackTo::Closes, false },
            { "24-settings-about", BackTo::Home, false },
            { "25-collection", BackTo::Home, true },
            { "26-collection-choosing", BackTo::Collection, false },
            { "27-figure-sheet", BackTo::Closes, false },
            { "28-figure-details", BackTo::Closes, false },
            { "29-figure-confirm", BackTo::StaysOpen, false },
            { "30-figure-edit", BackTo::StaysOpen, false },
            { "31-add-figure", BackTo::Closes, false },
            { "32-manage-trash", BackTo::Closes, false },
            { "33-manage-backups", BackTo::Closes, false },
            { "34-manage-import", BackTo::Closes, false },
            { "35-install-ready", BackTo::Home, false },
            { "36-profile-menu", BackTo::Closes, false },
            { "37-profile-editor", BackTo::Closes, false },
            { "38-settings-profiles", BackTo::Home, false },
            { "39-file-check", BackTo::Closes, false },
            { "40-game-trapteam", BackTo::Home, true },
            { "41-game-superchargers", BackTo::Home, true },
            { "42-game-imaginators", BackTo::Home, true },
            { "43-trapteam-game", BackTo::GamePage, false },
            { "44-trapteam-controller", BackTo::GamePage, false },
            { "45-superchargers-game", BackTo::GamePage, false },
            { "46-superchargers-controller", BackTo::GamePage, false },
            { "47-imaginators-game", BackTo::GamePage, false },
            { "48-imaginators-controller", BackTo::GamePage, false },
            { "49-portable-not-installed", BackTo::GamePage, false },
            { "50-ssa-sound", BackTo::GamePage, false },
        };
        ImGuiContext& g = *ImGui::GetCurrentContext();
        ImGuiIO& io = ImGui::GetIO();
        PadWalk& w = m_walk;
        // "quick" (ArkeyanLauncherTest): fewer frames between steps, a view walked only until every stop is reached, and the
        // long lists (Add a figure's catalogue, a uniform grid) only for 300 moves. Without it a long list is walked until every
        // stop is reached too (its four moves each would take half an hour).
        static const bool quick = std::string_view(LauncherEnv("SKY_LAUNCHER_TEST_PADWALK")) == "quick";
        constexpr int QuickBound = 300, LongList = 100;
        auto fail = [&](const std::string& what) {
            fprintf(stderr, "FAIL padwalk %s: %s\n", Views[w.view].name, what.c_str());
            m_testFailures++;
        };
        if (m_testBegan == 0)
            m_testBegan = now;
        if (now - m_testBegan > (quick ? 900000u : 3600000u))
        {
            fprintf(stderr, "FAIL padwalk: timeout\n");
            m_testFailures++;
            m_running = false;
            return;
        }
        if (w.view < 0)
        {
            // The games' discs: the test's disc for Spyro's Adventure (when given), none for the others.
            m_s->collection->Seed();
            if (m_s->disc == DiscState::None && !m_testDrop.empty())
                StartVerify(m_testDrop);
            if (m_s->disc == DiscState::Verifying)
                return;
            w.view = 0;
            w.phase = 0;
        }
        if (w.view >= int(std::size(Views)))
        {
            fprintf(stderr, "padwalk: %d views, %d failures\n", int(std::size(Views)), m_testFailures);
            m_running = false;
            return;
        }
        if (w.wait > 0)
        {
            w.wait--;
            return;
        }
        const WalkView& view = Views[w.view];
        const std::string_view name = view.name;
        auto game = [&](const char* id) {
            if (m_s->game.id != std::string_view(id))
                SelectGame(*FindGame(id));
        };
        auto mainPages = [&]() {
            std::vector<MainPage> pages{ { Page::Home, {} } };
            for (const LauncherGameEntry& entry : LauncherGameCatalog(m_config))
                if (entry.enabled && FindGame(entry.id) != nullptr)
                    pages.push_back({ Page::Play, entry.id });
            pages.push_back({ Page::Collection, {} });
            pages.push_back({ Page::Settings, {} });
            return pages;
        };
        auto mainIndex = [&]() {
            const std::vector<MainPage> pages = mainPages();
            for (size_t i = 0; i < pages.size(); i++)
                if (pages[i].page == m_page && (m_page != Page::Play || pages[i].game == m_s->game.id))
                    return int(i);
            return -1;
        };
        auto press = [&](ImGuiKey key) {
            io.AddKeyEvent(key, true);
            w.release = key;
        };

        switch (w.phase)
        {
        case 0: // the view, as a pad player arrives on it
        {
            // SKY_LAUNCHER_TEST_PADWALK_VIEW=<prefix>[,<prefix>...]: those views only ("26-", "2", "25-,26-").
            if (const char* only = LauncherEnv("SKY_LAUNCHER_TEST_PADWALK_VIEW"))
            {
                bool wanted = false;
                for (std::string_view list = only; !list.empty() && !wanted;)
                {
                    const size_t comma = list.find(',');
                    wanted = name.starts_with(list.substr(0, comma));
                    list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
                }
                if (!wanted)
                {
                    w.view++;
                    return;
                }
            }
            m_testPadInput = true;
            m_s->collection->TestScreen(0);
            m_reportOpen = false;
            m_portableAsk = false;
            m_un = {};
            w.hover = 0;
            m_drawn.erase("figure-hover");
            m_integrityOpen = false;
            m_findings.clear();
            m_profileEditor.open = false;
            if (name == "00-first-start")
                ; // as the launcher opens: Home, the focus in the sidebar
            else if (name == "01-home")
                GoTo(Page::Home);
            else if (name.starts_with("02-") || name.starts_with("05-"))
            {
                game("ssa");
                GoTo(Page::Play);
                m_testControllers = name.starts_with("05-");
            }
            else if (name.starts_with("03-"))
            {
                game("giants");
                GoTo(Page::Play);
            }
            else if (name.starts_with("04-"))
            {
                game("swapforce");
                GoTo(Page::Play);
            }
            else if (name.find("-ssa-") != std::string_view::npos || name.starts_with("06-") || name.starts_with("13-"))
            {
                static const std::pair<const char*, GameTab> Tabs[] = { { "video", GameTab::Video }, { "keyboard", GameTab::Keyboard },
                    { "controller", GameTab::Controller }, { "sound", GameTab::Sound }, { "language", GameTab::Language }, { "disc", GameTab::Disc },
                    { "saves", GameTab::Saves }, { "game", GameTab::Game } };
                game("ssa");
                GoTo(Page::GameSettings);
                m_gameTab = GameTab::Game;
                for (const auto& [tab, value] : Tabs)
                    if (name.ends_with(tab))
                        m_gameTab = value;
                m_portableAsk = name.starts_with("13-");
                if (m_portableAsk) // the copy is offered for an installed game with its disc only (CI runs with no disc image)
                {
                    m_s->installed.runtime = "padwalk";
                    w.discBefore = int(m_s->disc);
                    m_s->disc = DiscState::Ready;
                }
            }
            else if (name.starts_with("14-") || name.starts_with("15-"))
            {
                game("giants");
                GoTo(Page::GameSettings);
                m_gameTab = name.starts_with("14-") ? GameTab::Keyboard : GameTab::Controller;
            }
            else if (name.starts_with("16-") || name.starts_with("17-"))
            {
                game("swapforce");
                GoTo(Page::GameSettings);
                m_gameTab = name.starts_with("16-") ? GameTab::Keyboard : GameTab::Game;
            }
            else if (name.starts_with("18-"))
            {
                game("ssa");
                RefreshProgress(true);
                GoTo(m_progress.hasList ? Page::Achievements : Page::Play);
            }
            else if (name.starts_with("19-") || name.starts_with("20-") || name.starts_with("21-") || name.starts_with("22-") ||
                name.starts_with("23-") || name.starts_with("24-"))
            {
                GoTo(Page::Settings);
                m_tab = name.starts_with("19-") ? SettingsTab::Controls : name.starts_with("20-") || name.starts_with("21-") ? SettingsTab::Launcher
                    : name.starts_with("24-") ? SettingsTab::About : SettingsTab::Support;
                if (name.starts_with("21-"))
                    OpenUninstall();
                if (name.starts_with("23-"))
                    OpenProblemReport();
            }
            else if (name.starts_with("35-"))
            {
                game("ssa");
                GoTo(Page::Play);
                m_s->installed.runtime.clear();
            }
            else if (name.starts_with("36-") || name.starts_with("37-") || name.starts_with("38-"))
            {
                if (m_profiles.profiles.size() < 2) // a second profile, to have something to switch to
                    profiles::Add(m_profiles, "Kaos", 7);
                GoTo(name.starts_with("38-") ? Page::Settings : Page::Home);
                m_tab = SettingsTab::Profiles;
                m_testOpenProfileMenu = name.starts_with("36-");
                if (name.starts_with("37-"))
                    OpenProfileEditor({});
            }
            else if (name.starts_with("39-"))
            {
                game("ssa");
                GoTo(Page::Play);
                using Kind = filelist::Problem::Kind;
                ShowFinding({ "ssa", "ssa-game", { { Kind::Missing, "game/data.bin" }, { Kind::Changed, "game/engine.dat" } } });
            }
            else if (name.starts_with("40-") || name.starts_with("41-") || name.starts_with("42-"))
            {
                game(name.starts_with("40-") ? "trapteam" : name.starts_with("41-") ? "superchargers" : "imaginators");
                GoTo(Page::Play);
            }
            else if (name.starts_with("49-"))
            {
                game("ssa");
                GoTo(Page::GameSettings);
                m_gameTab = GameTab::Game;
                m_s->installed.runtime.clear();
                m_portableAsk = true; // set as a dialog left open on an earlier page would be
            }
            else if (name.size() > 2 && name[0] == '4' && name[1] >= '3' && name[1] <= '8')
            {
                game(name[1] <= '4' ? "trapteam" : name[1] <= '6' ? "superchargers" : "imaginators");
                GoTo(Page::GameSettings);
                m_gameTab = name.ends_with("controller") ? GameTab::Controller : GameTab::Game;
            }
            else
            {
                // The collection: its screens as its own screenshot test sets them (CollectionPage::TestScreen).
                static const std::pair<const char*, int> Screens[] = { { "25-", 0 }, { "26-", 1 }, { "27-", 4 }, { "28-", 5 }, { "29-", 6 },
                    { "30-", 7 }, { "31-", 8 }, { "32-", 10 }, { "33-", 11 }, { "34-", 10 } };
                GoTo(Page::Collection);
                for (const auto& [prefix, screen] : Screens)
                    if (name.starts_with(prefix))
                        m_s->collection->TestScreen(screen);
                if (name.starts_with("34-"))
                    m_s->collection->TestManageTab(2);
            }
            if (name != "00-first-start")
                m_focusShow = true;
            m_testShot = view.name;
            m_testAt = now + (quick ? 500 : 900);
            w.phase = 1;
            return;
        }
        case 1: // the stops of the window the focus is in, from the frame just drawn
        {
            // The pointer rests on a figure's card, then the pad moves the focus: the card's tip ("Click: sheet...") stays hidden.
            if (name.starts_with("25-") && w.hover < 3)
            {
                if (w.hover == 0)
                {
                    const auto card = std::find_if(w.frameStops.begin(), w.frameStops.end(), [&](const Stop& s) {
                        return s.shown && s.root == RootOf(g.NavWindow) && s.max.x - s.min.x > 90 && s.max.y - s.min.y > 110 && s.min.x > 300;
                    });
                    if (card != w.frameStops.end())
                        io.AddMousePosEvent(MidX(*card), MidY(*card));
                    w.hover = card != w.frameStops.end() ? 1 : 3;
                    w.wait = 3;
                }
                else if (w.hover == 1)
                {
                    press(ImGuiKey_GamepadDpadRight);
                    w.hover = 2;
                    w.wait = 2;
                }
                else
                {
                    io.AddKeyEvent(w.release, false);
                    const auto tip = m_drawn.find("figure-hover");
                    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
                    w.hover = 3;
                    w.wait = 2;
                    if (tip != m_drawn.end() && tip->second == "bubble")
                        fail("a figure's tip shows while the pad moves the focus");
                    else if (tip == m_drawn.end())
                        fail("no card was under the pointer: the tip check did not run");
                    else
                        fprintf(stderr, "padwalk %s: the tip stays hidden over a card under the pointer while the pad moves the focus\n", view.name);
                }
                return;
            }
            w.root = RootOf(g.NavWindow);
            w.expected.clear();
            for (const Stop& stop : w.frameStops)
            {
                if (stop.root != w.root)
                    continue;
                if (std::find(w.expected.begin(), w.expected.end(), stop.id) == w.expected.end())
                    w.expected.push_back(stop.id);
                else // two controls with one ImGui ID: the focus can't tell them apart
                {
                    const auto label = ui::Ctx().testNavigationLabels.find(stop.id);
                    fail(fmt::format("two stops share an ID ({})", label != ui::Ctx().testNavigationLabels.end() ? label->second : fmt::format("{:08x}", stop.id)));
                }
            }
            if (name.starts_with("33-")) // the focus follows the tab that was selected (the ring was left on "Trash")
            {
                const auto label = ui::Ctx().testNavigationLabels.find(g.NavId);
                if (label != ui::Ctx().testNavigationLabels.end() && label->second.starts_with("Trash"))
                    fail("the focus ring stays on the Trash tab while Backups is the active tab");
            }
            if (name.starts_with("49-") && m_portableAsk)
                fail("the portable copy opens for a game that is not installed");
            w.seen.assign(1, g.NavId);
            w.edges.clear();
            w.moves = 0;
            w.disorder = 0;
            w.disorderNotes.clear();
            w.pageAt = m_page;
            fprintf(stderr, "padwalk %s: %zu stops in %s\n", view.name, w.expected.size(), RootName(g.NavWindow));
            // The pad's hints at the foot of every page and dialog (a menu shows its rows only).
            if (!name.starts_with("05-") && !name.starts_with("36-") && std::find(w.frameLegends.begin(), w.frameLegends.end(), w.root) == w.frameLegends.end())
                fail("no pad legend");
            w.phase = 2;
            return;
        }
        case 2: // the next move: an unexplored direction here, else the way to the nearest stop that has one (every stop's four moves are tried)
        {
            const uint32_t focused = g.NavId;
            if (std::find(w.seen.begin(), w.seen.end(), focused) == w.seen.end())
                w.seen.push_back(focused);
            auto unexplored = [&](uint32_t id) {
                for (int i = 0; i < 4; i++)
                    if (!w.edges.contains({ id, i }))
                        return i;
                return -1;
            };
            const bool done = (quick || w.expected.size() > LongList) && std::all_of(w.expected.begin(), w.expected.end(),
                [&](uint32_t id) { return std::find(w.seen.begin(), w.seen.end(), id) != w.seen.end(); });
            const int bound = quick && w.expected.size() > LongList ? QuickBound : std::max(600, 6 * int(w.expected.size()));
            int direction = !done && w.moves < bound && m_page == w.pageAt ? unexplored(focused) : -2;
            if (direction == -1)
            {
                std::vector<std::pair<uint32_t, int>> queue{ { focused, -1 } };
                std::vector<uint32_t> visited{ focused };
                for (size_t at = 0; at < queue.size() && direction < 0; at++)
                {
                    const auto [id, firstMove] = queue[at];
                    if (id != focused && unexplored(id) >= 0)
                    {
                        direction = firstMove;
                        break;
                    }
                    for (int i = 0; i < 4; i++)
                    {
                        const auto edge = w.edges.find({ id, i });
                        if (edge == w.edges.end() || std::find(visited.begin(), visited.end(), edge->second) != visited.end())
                            continue;
                        visited.push_back(edge->second);
                        queue.push_back({ edge->second, firstMove < 0 ? i : firstMove });
                    }
                }
            }
            if (direction < 0)
            {
                // Explored: what the D-pad never reached.
                std::string unreached;
                int count = 0;
                for (uint32_t id : w.expected)
                    if (std::find(w.seen.begin(), w.seen.end(), id) == w.seen.end())
                    {
                        const auto label = ui::Ctx().testNavigationLabels.find(id);
                        unreached += fmt::format("{}{}", count++ ? ", " : "", label != ui::Ctx().testNavigationLabels.end() ? label->second : fmt::format("{:08x}", id));
                    }
                fprintf(stderr, "padwalk %s: reached %zu of %zu stops in %d moves; %d moves out of reading order\n", view.name,
                    w.expected.size() - count, w.expected.size(), w.moves, w.disorder);
                for (const std::string& note : w.disorderNotes)
                    fprintf(stderr, "padwalk %s: order: %s\n", view.name, note.c_str());
                if (w.disorder > 0)
                    fail(fmt::format("{} D-pad move(s) out of reading order", w.disorder));
                if (count > 0 && quick && w.expected.size() > LongList)
                    fprintf(stderr, "padwalk %s: a long list, walked for %d moves only (quick)\n", view.name, QuickBound);
                else if (count > 0)
                    fail(fmt::format("{} stop(s) the D-pad never reaches: {}", count, unreached));
                if (m_page != w.pageAt)
                    fail("a D-pad move changed the page");
                w.phase = 5;
                return;
            }
            static const ImGuiKey Keys[] = { ImGuiKey_GamepadDpadDown, ImGuiKey_GamepadDpadRight, ImGuiKey_GamepadDpadUp, ImGuiKey_GamepadDpadLeft };
            w.from = focused;
            w.direction = direction;
            w.stopsAt = w.frameStops; // the order is judged on the page as it was when the move was made (a list scrolls after)
            press(Keys[direction]);
            w.moves++;
            w.phase = 3;
            return;
        }
        case 3:
            io.AddKeyEvent(w.release, false);
            w.wait = quick ? 1 : 2;
            w.phase = 4;
            return;
        case 4:
            w.edges[{ w.from, w.direction }] = g.NavId;
            if (RootOf(g.NavWindow) == w.root)
                if (std::string note = Disorder(w.stopsAt, w.from, g.NavId, w.direction, ui::Ctx().testNavigationLabels); !note.empty())
                {
                    w.disorder++;
                    if (w.disorderNotes.size() < 6)
                        w.disorderNotes.push_back(note);
                }
            w.phase = 2;
            return;
        case 5: // B
            press(ImGuiKey_GamepadFaceRight);
            w.phase = 6;
            return;
        case 6:
            io.AddKeyEvent(w.release, false);
            w.wait = 4;
            w.phase = 7;
            return;
        case 7:
        {
            const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
            if (name.starts_with("13-"))
            {
                m_s->installed.runtime.clear();
                if (w.discBefore >= 0)
                    m_s->disc = DiscState(w.discBefore);
                w.discBefore = -1;
            }
            bool ok = true;
            BackTo back = view.back;
            if (name.starts_with("21-")) // the uninstall list is a step of the Launcher tab: B closes it, Settings stays
                ok = m_page == Page::Settings && m_un.stage == UninstallStage::Closed;
            else if (name.starts_with("18-") && w.pageAt != Page::Achievements)
                ok = true; // no achievements without the disc's list: the game page was shown
            else
                switch (back)
                {
                case BackTo::Sidebar: ok = std::string_view(RootName(g.NavWindow)) == "##sidebar"; break;
                case BackTo::Home: ok = m_page == Page::Home; break;
                case BackTo::GamePage: ok = m_page == Page::Play; break;
                case BackTo::Closes: ok = !popup && !m_reportOpen && !m_portableAsk && !m_integrityOpen && m_page == w.pageAt; break;
                case BackTo::StaysOpen: ok = popup && m_page == w.pageAt; break;
                case BackTo::Collection: ok = m_page == Page::Collection && !m_s->collection->TakesBack(); break;
                }
            if (!ok)
                fail(fmt::format("B should go back {}", name.starts_with("21-") ? "out of the list, Settings staying" : BackName(back)));
            else
                fprintf(stderr, "padwalk %s: B goes back\n", view.name);
            if (!view.mainPage)
            {
                w.view++;
                w.phase = 0;
                return;
            }
            // LB / RB from the view itself.
            w.pagesStep = 0;
            w.phase = 8;
            // Back on the view (B moved away): set it up again without a shot.
            if (name == "01-home")
                GoTo(Page::Home);
            else if (name.starts_with("02-"))
            {
                game("ssa");
                GoTo(Page::Play);
            }
            else if (name.starts_with("03-"))
            {
                game("giants");
                GoTo(Page::Play);
            }
            else if (name.starts_with("04-"))
            {
                game("swapforce");
                GoTo(Page::Play);
            }
            else if (name.starts_with("40-") || name.starts_with("41-") || name.starts_with("42-"))
            {
                game(name.starts_with("40-") ? "trapteam" : name.starts_with("41-") ? "superchargers" : "imaginators");
                GoTo(Page::Play);
            }
            else if (name.starts_with("19-"))
                GoTo(Page::Settings);
            else if (name.starts_with("25-"))
                GoTo(Page::Collection);
            w.wait = 6;
            return;
        }
        case 8: // RB (LB on the last page), then the other one back
        {
            const std::vector<MainPage> pages = mainPages();
            w.indexAt = mainIndex();
            // RB then LB back; LB then RB on the last page.
            w.stepDir = w.pagesStep == 0 ? (w.indexAt == int(pages.size()) - 1 ? -1 : 1) : -w.stepDir;
            press(w.stepDir > 0 ? ImGuiKey_GamepadR1 : ImGuiKey_GamepadL1);
            w.phase = 9;
            return;
        }
        case 9:
            io.AddKeyEvent(w.release, false);
            w.wait = 6;
            w.phase = 10;
            return;
        case 10:
        {
            const int expected = w.indexAt + w.stepDir;
            const int index = mainIndex();
            const char* button = w.stepDir > 0 ? "RB" : "LB";
            if (index != expected)
                fail(fmt::format("{} should open the {} main page (page {} of {}), it shows {}", button, w.stepDir > 0 ? "next" : "previous",
                    expected + 1, mainPages().size(), index + 1));
            else
                fprintf(stderr, "padwalk %s: %s opens the %s page\n", view.name, button, w.stepDir > 0 ? "next" : "previous");
            if (++w.pagesStep < 2 && index == expected)
            {
                w.phase = 8;
                return;
            }
            w.view++;
            w.phase = 0;
            return;
        }
        default:
            return;
        }
    }
}
