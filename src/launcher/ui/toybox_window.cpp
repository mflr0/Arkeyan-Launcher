// The Toy Box window (docs/toybox_window.md): the running game's Toy Box in a window of its own, to keep beside the game (a
// second monitor). It is this launcher started again with --toybox <game>: the Collection page alone, its seats live over
// the link to the game (launcher/toybox_link_client.h). The launcher opens it (the On the portal card's button, or with every
// game when "Open with the game" is ticked); it closes when the game ends. Its place and size, and the option, are kept in
// the launcher's data folder (toybox-window.ini), its own file (the launcher's settings file stays the launcher's).
#include "launcher/shortcut.h"
#include "launcher/ui/launcher_app.h"
#include "shared/collection/datadir.h"
#include "launcher/env.h"
#include <stb_image_write.h>
#include <SDL3/SDL.h>
#include <climits>
#include <cstdio>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace launcher_ui
{
    namespace
    {
        fs::path PlaceFile() { return LauncherDataFolder() / "toybox-window.ini"; }
        // The launcher asks a window already open to come to the front by leaving this
        // file; the window removes it.
        fs::path RaiseFile() { return LauncherDataFolder() / "toybox-window.raise"; }

        struct Place
        {
            int x = INT_MIN, y = INT_MIN, w = 0, h = 0;
            bool withGame = false;
        };

        Place ReadPlace()
        {
            Place place;
            std::ifstream in(PlaceFile());
            std::string line;
            while (std::getline(in, line))
            {
                const size_t equals = line.find('=');
                if (equals == std::string::npos)
                    continue;
                const std::string key = line.substr(0, equals);
                const int value = atoi(line.c_str() + equals + 1);
                if (key == "x")
                    place.x = value;
                else if (key == "y")
                    place.y = value;
                else if (key == "w")
                    place.w = value;
                else if (key == "h")
                    place.h = value;
                else if (key == "with_game")
                    place.withGame = value != 0;
            }
            return place;
        }

        void WritePlace(const Place& place)
        {
            std::error_code error;
            fs::create_directories(PlaceFile().parent_path(), error);
            const fs::path temporary = PlaceFile().string() + ".tmp";
            {
                std::ofstream out(temporary, std::ios::trunc);
                out << "# The Toy Box window: where it was, and whether it opens with the "
                       "game\n";
                if (place.w > 0 && place.h > 0)
                    out << fmt::format("x={}\ny={}\nw={}\nh={}\n", place.x, place.y, place.w, place.h);
                out << fmt::format("with_game={}\n", place.withGame ? 1 : 0);
            }
            fs::rename(temporary, PlaceFile(), error);
        }

        // The saved place, when it is still on one of the displays (a monitor unplugged
        // since puts it back in the middle).
        bool OnADisplay(const Place& place)
        {
            if (place.w <= 0 || place.h <= 0 || place.x == INT_MIN)
                return false;
            int count = 0;
            SDL_DisplayID* displays = SDL_GetDisplays(&count);
            bool found = false;
            for (int i = 0; displays != nullptr && i < count && !found; i++)
            {
                SDL_Rect bounds;
                if (SDL_GetDisplayUsableBounds(displays[i], &bounds))
                    found = place.x + 80 > bounds.x && place.x < bounds.x + bounds.w - 80 && place.y >= bounds.y - 10 &&
                            place.y < bounds.y + bounds.h - 80;
            }
            SDL_free(displays);
            return found;
        }

        // A refused command, in the player's words (the game sends the reason, toybox_link.h).
        std::string RefusalText(const std::string& result)
        {
            if (result == toyboxlink::refusal::Real)
                return Tr("A real figure is there: lift it off the portal first");
            if (result == toyboxlink::refusal::Busy)
                return Tr("Another program is using this figure");
            if (result == toyboxlink::refusal::CannotRead)
                return Tr("The game cannot read this figure there");
            if (result == toyboxlink::refusal::Already)
                return Tr("It is already there");
            if (result == toyboxlink::refusal::Full)
                return Tr("The portal is full");
            if (result == toyboxlink::refusal::NoPortal)
                return Tr("The game has no virtual portal now");
            return Tr("The game did not find this figure");
        }
    } // namespace

    bool App::ToyboxWithGame(bool fresh) const
    {
        if (fresh || m_toyboxWithGame < 0)
            m_toyboxWithGame = ReadPlace().withGame ? 1 : 0;
        return m_toyboxWithGame == 1;
    }

    void App::SetToyboxWithGame(bool on)
    {
        Place place = ReadPlace();
        place.withGame = on;
        WritePlace(place);
        m_toyboxWithGame = on ? 1 : 0;
    }

    // ---- In the launcher ----------------------------------------------------------------------------------------------

    void App::OpenToyboxWindow()
    {
        const std::string game = m_session.Running();
        if (game.empty())
        {
            Toast(Tr("Start a game first"));
            return;
        }
        if (m_toyboxProcess != nullptr)
        {
            int code = 0;
            if (!SDL_WaitProcess(m_toyboxProcess, false, &code))
            {
                // Open already: to the front.
                std::ofstream(RaiseFile()) << game << "\n";
                return;
            }
            SDL_DestroyProcess(m_toyboxProcess);
            m_toyboxProcess = nullptr;
        }
        const std::string self = datadir::Text(SelfExecutable());
        const char* arguments[] = { self.c_str(), "--toybox", game.c_str(), nullptr };
        m_toyboxProcess = SDL_CreateProcess(arguments, false);
        if (m_toyboxProcess == nullptr)
        {
            fprintf(stderr, "launcher: cannot open the Toy Box window: %s\n", SDL_GetError());
            Toast(Tr("Couldn't open the Toy Box window"), ui::ToastKind::Error);
            return;
        }
        fprintf(stderr, "launcher: Toy Box window opened for %s\n", game.c_str());
    }

    void App::PollToyboxWindow()
    {
        const std::string& running = m_session.Running();
        if (running.empty())
            m_toyboxOpenedFor.clear();
        else if (running != m_toyboxOpenedFor)
        {
            // Once per game start: the window comes with the game when the player asked
            // for it.
            m_toyboxOpenedFor = running;
            if (m_session.Ours() && ToyboxWithGame(true))
                OpenToyboxWindow();
        }
        int code = 0;
        if (m_toyboxProcess != nullptr && SDL_WaitProcess(m_toyboxProcess, false, &code))
        {
            SDL_DestroyProcess(m_toyboxProcess);
            m_toyboxProcess = nullptr;
        }
    }

    // ---- In the window ------------------------------------------------------------------------------------------------

    void App::PlaceToyboxWindow()
    {
        const Place place = ReadPlace();
        if (OnADisplay(place))
        {
            SDL_SetWindowSize(m_window, place.w, place.h);
            SDL_SetWindowPosition(m_window, place.x, place.y);
        }
        else
        {
            SDL_SetWindowSize(m_window, 1000, 760);
            SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
        const std::string title = fmt::format("{} · {}", Tr("Toy Box"), GameName(m_toybox, GameNameForm::Short));
        SDL_SetWindowTitle(m_window, title.c_str());
        SDL_SetWindowMinimumSize(m_window, 640, 480);
    }

    void App::SaveToyboxPlace()
    {
        if (m_window == nullptr || (SDL_GetWindowFlags(m_window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_FULLSCREEN)) != 0)
            return;
        Place place = ReadPlace();
        SDL_GetWindowPosition(m_window, &place.x, &place.y);
        SDL_GetWindowSize(m_window, &place.w, &place.h);
        WritePlace(place);
    }

    void App::StartToyboxLink()
    {
        const collection::Game* game = collection::Catalogue::Get().FindGame(m_toybox);
        const std::string session = game != nullptr ? SessionFile(*game) : std::string();
        m_link = std::make_unique<toyboxlinkclient::Client>(toyboxlinkclient::LinkFileBeside(session), "toybox-window");
        m_linkState = m_link->Current();
        m_linkSince = SDL_GetTicks();
        m_s->collection->SetLive(m_toybox, m_link.get());
        fprintf(stderr, "launcher: Toy Box window for %s, waiting for the game\n", m_toybox.c_str());
    }

    void App::PollToyboxLink()
    {
        using State = toyboxlinkclient::Client::State;
        const uint64_t now = SDL_GetTicks();
        const State state = m_link->Current();
        if (state != m_linkState)
        {
            static const char* const Names[] = { "waiting", "connected", "other version", "another window", "ended" };
            fprintf(stderr, "launcher: Toy Box window: %s\n", Names[int(state)]);
            if (state == State::Connected && !m_linkSeen)
            {
                m_linkSeen = true;
                m_toyboxConnectedAt = now;
            }
            m_linkState = state;
            m_linkSince = now;
            m_s->collection->LinkChanged();
            m_busyFrames = 3;
        }
        toyboxlinkclient::Client::Done done;
        while (m_link->NextDone(done))
        {
            fprintf(stderr, "launcher: Toy Box window: %s %d: %s\n", done.verb.c_str(), done.seat, done.result.c_str());
            if (done.result != "ok")
                Toast(RefusalText(done.result), ui::ToastKind::Error);
            m_busyFrames = 3;
        }
        if (m_link->Generation() != m_linkGeneration)
        {
            m_linkGeneration = m_link->Generation();
            m_busyFrames = 3; // the seats changed: drawn now
        }
        // The game ends: so does its window. A game that went without a word (a crash) is seen by its marker.
        if (state == State::Ended)
            m_running = false;
        else if (state == State::Waiting && now - m_linkSince > (m_linkSeen ? 3000u : 15000u) &&
                 !GameIsRunning(FoldersOf(m_toybox)))
            m_running = false;
        if (now >= m_nextRaiseLook)
        {
            m_nextRaiseLook = now + 500;
            std::error_code error;
            if (fs::exists(RaiseFile(), error))
            {
                fs::remove(RaiseFile(), error);
                SDL_RestoreWindow(m_window);
                SDL_RaiseWindow(m_window);
            }
        }
        if (!m_running)
        {
            fprintf(stderr, "launcher: Toy Box window closes: the game ended\n");
            SaveToyboxPlace();
        }
    }

    bool App::ToyboxGameWithoutLink() const
    {
        if (m_link == nullptr || m_linkSeen || m_linkState != toyboxlinkclient::Client::State::Waiting ||
            SDL_GetTicks() - m_linkSince < 15000)
            return false;
        const collection::Game* game = collection::Catalogue::Get().FindGame(m_toybox);
        std::error_code error;
        return game != nullptr && !fs::exists(datadir::FromText(toyboxlinkclient::LinkFileBeside(SessionFile(*game))), error);
    }

    void App::DrawToyboxPage()
    {
        DrawBackdrop();
        const ImVec2 size = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(Pad + 8, Pad + 4));
        ImGui::Begin("##page", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        m_s->collection->Draw();
        ImGui::End();
        ImGui::PopStyleVar();
        DrawToast();
        m_pagesTaken = true; // no other page to go to
        m_backTaken = true; // B / Escape: the page's own (an overlay, a seat being
        // chosen, the search), never leaving it
        m_focusTo = FocusTo::None;
    }

    void App::RunToyboxScript()
    {
        static bool loaded = false;
        if (!loaded)
        {
            loaded = true;
            if (const char* script = LauncherEnv("SKY_LAUNCHER_TEST_TOYBOX"))
            {
                std::stringstream items(script);
                std::string item;
                while (std::getline(items, item, '|'))
                    if (const size_t colon = item.find(':'); colon != std::string::npos)
                        m_toyboxScript.push_back({ atof(item.substr(0, colon).c_str()), item.substr(colon + 1) });
            }
        }
        if (m_toyboxNext >= m_toyboxScript.size() || m_toyboxConnectedAt == 0 || !m_toyboxShot.empty())
            return;
        const ToyboxStep& step = m_toyboxScript[m_toyboxNext];
        if (double(SDL_GetTicks() - m_toyboxConnectedAt) / 1000.0 < step.at)
            return;
        m_toyboxNext++;
        m_busyFrames = 3;
        std::stringstream words(step.action);
        std::string verb, file;
        int seat = -1;
        words >> verb >> seat;
        std::getline(words >> std::ws, file);
        fprintf(stderr, "launcher: Toy Box window test: %s\n", step.action.c_str());
        if (verb == "put" && seat >= 0)
            m_s->collection->TestPutOn(size_t(seat), collection::PathOf(file));
        else if (verb == "off" && seat >= 0)
            m_s->collection->TestTakeOff(size_t(seat));
        else if (verb == "choose")
            m_s->collection->TestChoose(seat);
        else if (step.action.rfind("shot ", 0) == 0)
            m_toyboxShot = step.action.substr(5);
        else if (verb == "quit")
            m_running = false;
        else
            fprintf(stderr, "launcher: Toy Box window test: unknown step \"%s\"\n", step.action.c_str());
    }

    void App::ToyboxShot()
    {
        if (m_toyboxShot.empty())
            return;
        const char* folder = LauncherEnv("SKY_LAUNCHER_TOYBOX_SHOTS");
        SDL_Surface* surface = folder != nullptr ? SDL_RenderReadPixels(m_renderer, nullptr) : nullptr;
        if (surface != nullptr)
        {
            if (SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32))
            {
                std::error_code error;
                fs::create_directories(datadir::FromText(folder), error);
                const std::string path = std::string(folder) + "/" + m_toyboxShot + ".png";
                if (stbi_write_png(path.c_str(), rgba->w, rgba->h, 4, rgba->pixels, rgba->pitch))
                    fprintf(stderr, "launcher: shot %s\n", path.c_str());
                SDL_DestroySurface(rgba);
            }
            SDL_DestroySurface(surface);
        }
        m_toyboxShot.clear();
    }
}
