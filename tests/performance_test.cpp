// Synthetic fixtures only. Run with every data directory redirected to a scratch tree.
#include "test_env.h"
#include "launcher/env.h"
#include "launcher/ui/launcher_app.h"
#include "shared/collection/datadir.h"
#include "shared/collection/icons.h"
#include "shared/collection/json.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <limits>
#include <stb_image.h>
#include <set>
#if defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#include <sys/resource.h>
#include <unistd.h>
#elif !defined(_WIN32)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace launcher_ui
{
    struct PerformanceProbe
    {
        using Clock = std::chrono::steady_clock;
        static double Ms(Clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        }
        struct Usage
        {
            double cpu = std::numeric_limits<double>::quiet_NaN(), rss = cpu, read = cpu, write = cpu;
        };
        static Usage Resources()
        {
            Usage out;
#if !defined(_WIN32)
            rusage usage{};
            getrusage(RUSAGE_SELF, &usage);
            out.cpu = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6 + usage.ru_stime.tv_sec +
                      usage.ru_stime.tv_usec / 1e6;
#if defined(__APPLE__)
            mach_task_basic_info_data_t info{};
            mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
            if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
                KERN_SUCCESS)
                out.rss = double(info.resident_size);
            rusage_info_v2 disk{};
            if (proc_pid_rusage(getpid(), RUSAGE_INFO_V2, reinterpret_cast<rusage_info_t*>(&disk)) == 0)
                out.read = double(disk.ri_diskio_bytesread), out.write = double(disk.ri_diskio_byteswritten);
#else
            std::ifstream stat("/proc/self/statm");
            size_t total = 0, resident = 0;
            stat >> total >> resident;
            out.rss = double(resident) * sysconf(_SC_PAGESIZE);
#endif
#endif
            return out;
        }
        static void Seed()
        {
            using collection::Json;
            Json root = Json::Object(), toys = Json::Array(), games = Json::Array();
            const char* ids[] = { "ssa", "giants", "swapforce", "trapteam" };
            for (int i = 0; i < 500; ++i)
            {
                Json toy = Json::Object();
                toy.Set("type", 50000 + i);
                toy.Set("name", "Fixture " + std::to_string(i));
                toy.Set("element", "magic");
                toy.Set("category", "core");
                toy.Set("game", ids[i % 4]);
                toys.Push(toy);
            }
            for (int g = 0; g < 4; ++g)
            {
                Json game = Json::Object(), portraits = Json::Object(), accepted = Json::Array(), seats = Json::Array();
                game.Set("id", ids[g]);
                game.Set("name", "Fixture game " + std::to_string(g));
                for (int i = 0; i < 500; ++i)
                {
                    accepted.Push(Json(50000 + i));
                    if (i % 4 == g)
                        portraits.Set(std::to_string(50000 + i), "fixture-" + std::to_string(i));
                }
                for (int i = 0; i < 2; ++i)
                {
                    Json seat = Json::Object(), accepts = Json::Array();
                    accepts.Push(Json("core"));
                    seat.Set("id", "p" + std::to_string(i + 1));
                    seat.Set("player", i + 1);
                    seat.Set("accepts", accepts);
                    seats.Push(seat);
                }
                game.Set("accepts", accepted);
                game.Set("portraits", portraits);
                game.Set("seats", seats);
                games.Push(game);
            }
            root.Set("format", 1);
            root.Set("toys", toys);
            root.Set("games", games);
            std::filesystem::create_directories(collection::Folder());
            std::ofstream(collection::Folder() + "/catalogue.json") << root.Dump();
            collection::Image image;
            image.width = image.height = 256;
            image.rgba.resize(256 * 256);
            uint32_t random = 7;
            for (int i = 0; i < 500; ++i)
            {
                auto tag = figures::CreateFigure(uint16_t(50000 + i));
                std::ofstream(collection::PathOf("fixture-" + std::to_string(i) + ".sky"), std::ios::binary)
                    .write(reinterpret_cast<const char*>(&tag), sizeof(tag));
                for (auto& pixel : image.rgba)
                {
                    random = random * 1664525 + 1013904223;
                    pixel = 0xff000000 | (random & 0xffffff);
                }
                auto folder = datadir::Game(ids[i % 4]) / "portraits";
                std::filesystem::create_directories(folder);
                if (!collection::SavePng(image, datadir::Text(folder / ("fixture-" + std::to_string(i) + ".png"))))
                    throw std::runtime_error("fixture picture failed");
            }
        }
        static void Tick(App& app, bool wait)
        {
            SDL_Event event;
            if (wait)
                if (int delay = app.IdleWait(); delay > 0 && SDL_WaitEventTimeout(&event, delay))
                    app.HandleEvent(event);
            while (SDL_PollEvent(&event))
                app.HandleEvent(event);
            app.PollSlots();
            app.PollGame();
            app.CheckQueuedDiscs();
            app.PollUpdateAll();
            app.PollUpdates();
            app.AutoPlay();
            app.Frame();
        }
        static SDL_Window* FocusWindow()
        {
            SDL_PropertiesID properties = SDL_CreateProperties();
            SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Timing focus");
            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 64);
            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 64);
            SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, SDL_WINDOW_HIDDEN);
            SDL_SetBooleanProperty(properties, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
            SDL_Window* window = SDL_CreateWindowWithProperties(properties);
            SDL_DestroyProperties(properties);
            return window;
        }
        template <class Cache>
        static auto& IconMap(Cache& cache)
        {
            if constexpr (requires { cache->textures; })
                return cache->textures;
            else
                return cache;
        }
        static double Textures(App& app)
        {
            double bytes = 0;
            std::set<SDL_Texture*> counted;
            auto add = [&](SDL_Texture* texture) {
                float w = 0, h = 0;
                if (texture && counted.insert(texture).second && SDL_GetTextureSize(texture, &w, &h))
                    bytes += w * h * 4;
            };
            for (const auto& slot : app.m_slots)
            {
                add(slot->icon);
                add(slot->logo.texture);
                for (const auto& [key, icon] : IconMap(slot->collection->m_icons))
                    add(icon.texture);
            }
            for (const auto& [key, art] : app.m_cachedArt)
                add(art.texture);
            for (const auto& [key, art] : app.m_achievementIcons)
                add(art.texture);
            for (auto* texture : ImGui::GetPlatformIO().Textures)
                bytes += texture->Width * texture->Height * 4;
            return bytes / (1024 * 1024);
        }
        static void Measure(App& app, const char* label)
        {
            for (int i = 0; i < 5; ++i)
                Tick(app, false);
            auto start = Clock::now();
            auto before = Resources();
            size_t frames = 0;
            while (Ms(start) < 1500)
            {
                Tick(app, true);
                ++frames;
            }
            double seconds = Ms(start) / 1000;
            auto after = Resources();
            printf("idle,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f,%.0f\n", label, 1000 * (after.cpu - before.cpu) / frames,
                100 * (after.cpu - before.cpu) / seconds, frames / seconds, after.rss / (1024 * 1024), Textures(app),
                (after.read - before.read) / seconds, (after.write - before.write) / seconds);
            fflush(stdout);
        }
        static int Run(bool check, bool memory, bool background)
        {
            auto start = Clock::now();
            LauncherConfig config = LoadLauncherConfig(skyenv::Get("SKY_LAUNCHER_INI"));
            config["launcher"]["experimental_superchargers"] = "1";
            config["collection"]["moved"] = "1";
            App app(*FindGame("ssa"), skyenv::Get("SKY_LAUNCHER_INI"), config);
            if (!app.Init())
                throw std::runtime_error(SDL_GetError());
            app.m_startGames = false;
            app.m_s->started = true;
            app.StartGameDisc();
            for (const auto& entry : LauncherGameCatalog(config))
                if (entry.enabled && FindGame(entry.id) && std::string_view(entry.id) != app.m_s->game.id)
                    app.m_discQueue.push_back(entry.id);
            Tick(app, false);
            printf("time,first_frame_ms,%.3f\n", Ms(start));
            fflush(stdout);
            if (background)
            {
                SDL_Window* sink = FocusWindow();
                if (!sink || !SDL_ShowWindow(sink))
                    throw std::runtime_error("focus window failed");
                if (SDL_GetWindowFlags(app.m_window) & SDL_WINDOW_INPUT_FOCUS)
                    throw std::runtime_error("launcher still has focus");
                Measure(app, "background_home");
                app.GoTo(Page::Play);
                Measure(app, "background_play");
                app.GoTo(Page::Collection);
                Measure(app, "background_collection");
                app.GoTo(Page::Home);
                start = Clock::now();
                auto before = Resources();
                int frames = 0;
                while (Ms(start) < 1500)
                {
                    ui::Ctx().animating = true;
                    Tick(app, true);
                    ++frames;
                }
                auto after = Resources();
                printf("animation,background_home,%.3f,%.3f\n", frames / (Ms(start) / 1000),
                    100 * (after.cpu - before.cpu) / (Ms(start) / 1000));
                const auto figures = collection::List();
                start = Clock::now();
                for (int i = 0; i < 100; ++i)
                {
                    auto path = collection::PortraitPath("ssa", *figures[size_t(i)].toy);
                    std::vector<uint8_t> png;
                    if (!ReadWholeFile(path, png))
                        throw std::runtime_error("fixture read failed");
                    int w = 0, h = 0, channels = 0;
                    auto* pixels = stbi_load_from_memory(png.data(), int(png.size()), &w, &h, &channels, 4);
                    if (!pixels)
                        throw std::runtime_error("fixture decode failed");
                    stbi_image_free(pixels);
                }
                printf("time,ui_picture_read_decode_ms,%.3f\n", Ms(start) / 100);
                start = Clock::now();
                before = Resources();
                for (int i = 0; i < 50; ++i)
                {
                    app.PollSlots();
                    app.PollGame();
                    app.CheckQueuedDiscs();
                    app.PollUpdateAll();
                    app.PollUpdates();
                }
                after = Resources();
                printf("profile,poll_ms,%.3f,%.3f\n", Ms(start) / 50, 1000 * (after.cpu - before.cpu) / 50);
                start = Clock::now();
                before = Resources();
                for (int i = 0; i < 10; ++i)
                    app.Frame();
                after = Resources();
                printf("profile,frame_ms,%.3f,%.3f\n", Ms(start) / 10, 1000 * (after.cpu - before.cpu) / 10);
                SDL_DestroyWindow(sink);
                SDL_HideWindow(app.m_window);
                SDL_ShowWindow(app.m_window);
                const bool minimized =
                    SDL_MinimizeWindow(app.m_window) && (SDL_GetWindowFlags(app.m_window) & SDL_WINDOW_MINIMIZED);
                printf("state,minimized_supported,%d\n", int(minimized));
                if (minimized)
                    Measure(app, "minimized_home");
                return 0;
            }
            if (memory)
            {
                for (const auto& game : g_games)
                {
                    app.SelectGame(game);
                    auto figures = collection::List();
                    auto& page = *app.m_s->collection;
                    start = Clock::now();
                    for (const auto& figure : figures)
                        page.Icon(figure.toy, figure.Kind(), false);
                    printf("cache,%s,%.3f,%.3f,%.3f\n", game.id, Ms(start), Resources().rss / (1024 * 1024),
                        Textures(app));
                }
                return 0;
            }
            if (check)
            {
                int failures = 0;
                auto expect = [&](bool ok, const char* what) {
                    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
                    failures += !ok;
                };
                expect(
                    App::VisibilityWait(0, SDL_WINDOW_INPUT_FOCUS) == 0, "foreground animation keeps display pacing");
                expect(App::VisibilityWait(33, SDL_WINDOW_INPUT_FOCUS) == 33, "foreground stars keep their cadence");
                expect(
                    App::VisibilityWait(500, SDL_WINDOW_INPUT_FOCUS) == 500, "foreground static page keeps idle wait");
                expect(App::VisibilityWait(0, 0) >= 250, "unfocused motion is throttled");
                for (auto flags : { SDL_WINDOW_HIDDEN, SDL_WINDOW_MINIMIZED, SDL_WINDOW_OCCLUDED })
                    expect(App::VisibilityWait(0, flags | SDL_WINDOW_INPUT_FOCUS) >= 500,
                        "unseen motion takes precedence over focus");
                for (int hidden = 0; hidden < 2; ++hidden)
                {
                    if (hidden)
                        SDL_HideWindow(app.m_window);
                    for (int i = 0; i < 5; ++i)
                        Tick(app, false);
                    if (hidden)
                    {
                        ui::Ctx().animating = true;
                        expect(
                            app.IdleWait() >= 500, "hidden animation requests a polling wait even on a loaded machine");
                    }
                    start = Clock::now();
                    int frames = 0;
                    const int drawnBefore = ImGui::GetFrameCount();
                    while (Ms(start) < 1100)
                    {
                        ui::Ctx().animating = true;
                        Tick(app, true);
                        ++frames;
                    }
                    double fps = frames / (Ms(start) / 1000);
                    printf("check_fps,%s,%.3f\n", hidden ? "hidden" : "visible", fps);
                    expect(fps <= (hidden || !(SDL_GetWindowFlags(app.m_window) & SDL_WINDOW_INPUT_FOCUS) ? 5 : 65),
                        hidden ? "hidden animation is throttled" : "visible animation is paced");
                    if (hidden)
                        expect(ImGui::GetFrameCount() == drawnBefore, "hidden window does not render");
                }
                expect(ImGui::GetFrameCount() > 0, "visible frames were rendered");
                SDL_ShowWindow(app.m_window);
                SDL_Window* sink = FocusWindow();
                expect(sink && SDL_ShowWindow(sink), "offscreen focus window opens");
                expect(!(SDL_GetWindowFlags(app.m_window) & SDL_WINDOW_INPUT_FOCUS), "launcher loses keyboard focus");
                for (int i = 0; i < 5; ++i)
                    Tick(app, false);
                ui::Ctx().animating = true;
                expect(app.IdleWait() >= 250, "focus loss requests a wait even on a loaded machine");
                start = Clock::now();
                int backgroundFrames = 0;
                while (Ms(start) < 1100)
                {
                    ui::Ctx().animating = true;
                    Tick(app, true);
                    ++backgroundFrames;
                }
                expect(backgroundFrames / (Ms(start) / 1000) <= 5, "real focus loss throttles animation");
                SDL_DestroyWindow(sink);
                SDL_HideWindow(app.m_window);
                SDL_ShowWindow(app.m_window);
                const int beforeRestore = ImGui::GetFrameCount();
                Tick(app, false);
                expect(ImGui::GetFrameCount() > beforeRestore, "restored window renders immediately");
                const auto figures = collection::List();
                expect(figures.size() == 500, "synthetic collection has 500 figures");
                if (figures.empty())
                    return 1;
                SDL_Texture* first = nullptr;
                for (const auto& game : g_games)
                {
                    app.SelectGame(game);
                    auto* texture =
                        app.m_s->collection->Icon(figures.front().toy, figures.front().Kind(), false).texture;
                    if (!first)
                        first = texture;
                    expect(texture && texture == first, "one picture texture is reused between game slots");
                }
                auto& page = *app.m_s->collection;
                page.ClearIcons();
                bool empty = true;
                for (auto& slot : app.m_slots)
                    empty = empty && IconMap(slot->collection->m_icons).empty();
                expect(empty, "disc refresh invalidates every slot's shared pictures");
                auto& icon = page.Icon(figures.front().toy, figures.front().Kind(), false);
                expect(icon.texture != nullptr && !icon.generated, "picture reload preserves the decoded image");
                SDL_Texture* different = page.Icon(figures.back().toy, figures.back().Kind(), false).texture;
                expect(different != nullptr && different != icon.texture, "distinct pictures keep distinct textures");
                return failures ? 1 : 0;
            }
            Measure(app, "home");
            for (const auto& game : g_games)
            {
                app.SelectGame(game);
                app.GoTo(Page::Play);
                Measure(app, game.id);
            }
            app.SelectGame(*FindGame("ssa"));
            app.GoTo(Page::Collection);
            start = Clock::now();
            Tick(app, false);
            printf("time,collection_open_ms,%.3f\n", Ms(start));
            auto& page = *app.m_s->collection;
            if (page.m_model.figures.size() != 500)
                throw std::runtime_error("expected 500 synthetic figures");
            start = Clock::now();
            std::strcpy(page.m_search, "Fixture 12");
            Tick(app, false);
            printf("time,filter_ms,%.3f\n", Ms(start));
            page.m_search[0] = 0;
            Tick(app, false);
            Measure(app, "collection");
            start = Clock::now();
            page.m_testScrollBottom = true;
            Tick(app, false);
            Tick(app, false);
            Tick(app, false);
            printf("time,scroll_bottom_ms,%.3f\n", Ms(start) / 3);
            page.m_testScrollBottom = false;
            for (int i = 0; i < 4; ++i)
            {
                app.GoTo(Page::Settings);
                app.m_tab = SettingsTab(i);
                Measure(app, ("settings_" + std::to_string(i)).c_str());
            }
            for (int i = 0; i < 8; ++i)
            {
                app.GoTo(Page::GameSettings);
                app.m_gameTab = GameTab(i);
                Measure(app, ("game_settings_" + std::to_string(i)).c_str());
            }
            app.GoTo(Page::Achievements);
            Measure(app, "achievements");
            app.GoTo(Page::Collection);
            SDL_HideWindow(app.m_window);
            Measure(app, "hidden_collection");
            app.GoTo(Page::Play);
            Measure(app, "hidden_play");
            SDL_ShowWindow(app.m_window);
            app.GoTo(Page::Home);
            // Exercise the component animation path without starting a game or a worker.
            start = Clock::now();
            auto before = Resources();
            int frames = 0;
            while (Ms(start) < 1500)
            {
                ui::Ctx().animating = true;
                Tick(app, true);
                ++frames;
            }
            auto after = Resources();
            printf("animation,home,%.3f,%.3f\n", frames / (Ms(start) / 1000),
                100 * (after.cpu - before.cpu) / (Ms(start) / 1000));
            auto figures = collection::List();
            start = Clock::now();
            for (int i = 0; i < 100; ++i)
            {
                CollectionModel model;
                model.gameId = "ssa";
                model.figures = figures;
                std::vector<collection::Figure> shown;
                model.Shelf({}, shown);
            }
            printf("time,shelf_ms,%.3f\n", Ms(start) / 100);
            start = Clock::now();
            for (int i = 0; i < 5; ++i)
                page.m_model.Load();
            printf("time,model_load_ms,%.3f\n", Ms(start) / 5);
            start = Clock::now();
            for (int i = 0; i < 50; ++i)
                page.m_model.Poll(UINT64_MAX - 100000 + uint64_t(i) * 1500);
            printf("time,signature_ms,%.3f\n", Ms(start) / 50);
            start = Clock::now();
            for (int i = 0; i < 100; ++i)
            {
                auto path = collection::PortraitPath("ssa", *figures[size_t(i)].toy);
                auto texture = LoadPngFile(app.m_renderer, path);
                if (texture)
                    SDL_DestroyTexture(texture);
            }
            printf("time,picture_read_decode_upload_ms,%.3f\n", Ms(start) / 100);
            start = Clock::now();
            for (int i = 0; i < 100; ++i)
            {
                auto path = collection::PortraitPath("ssa", *figures[size_t(i)].toy);
                auto image = collection::LoadPng(path);
                if (image.Empty())
                    throw std::runtime_error("decode failed");
            }
            printf("time,picture_read_decode_ms,%.3f\n", Ms(start) / 100);
            start = Clock::now();
            for (int i = 0; i < 100; ++i)
                SaveLauncherConfig(skyenv::Get("SKY_LAUNCHER_INI"), config);
            printf("time,config_save_ms,%.3f\n", Ms(start) / 100);
            return 0;
        }
    };
}

int main(int argc, char** argv)
{
    try
    {
        const auto scratch = std::filesystem::current_path() / ".scratch";
        for (const char* name : { "HOME", "CFFIXED_USER_HOME", "SKY_LAUNCHER_INI", "SKY_SETTINGS", "SKY_LAUNCHER_DATA",
                 "SKYLANDERS_DATA", "SKY_COLLECTION", "SKY_SAVE_DIR", "SKY_LAUNCHER_DESKTOP" })
        {
            if (!skyenv::Get(name) || !*skyenv::Get(name))
                throw std::runtime_error("scratch environment required");
            const auto relative = std::filesystem::absolute(datadir::FromText(skyenv::Get(name)))
                                      .lexically_normal()
                                      .lexically_relative(scratch);
            if (relative.empty() || *relative.begin() == "..")
                throw std::runtime_error("paths must stay under the clone's scratch folder");
        }
        for (auto [name, value] : { std::pair{ "SKY_LAUNCHER_NO_UPDATE_CHECK", "1" },
                 { "SDL_VIDEO_DRIVER", "offscreen" }, { "SDL_AUDIO_DRIVER", "dummy" } })
            if (!skyenv::Get(name) || std::string(skyenv::Get(name)) != value)
                throw std::runtime_error("offscreen, dummy audio and updates disabled required");
        if (argc == 2 && std::string_view(argv[1]) == "--seed")
        {
            launcher_ui::PerformanceProbe::Seed();
            return 0;
        }
        return launcher_ui::PerformanceProbe::Run(argc == 2 && std::string_view(argv[1]) == "--check",
            argc == 2 && std::string_view(argv[1]) == "--memory",
            argc == 2 && std::string_view(argv[1]) == "--background");
    }
    catch (const std::exception& error)
    {
        fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
