#pragma once

#include "launcher/ui/launcher_app.h"
#include "shared/collection/datadir.h"
#include <imgui_internal.h>
#include <thread>
#if !defined(_WIN32)
#include <sys/resource.h>
#include <csignal>
#endif

namespace launcher_ui
{
    // Each UI case runs in its own process, including its ImGui context and scratch environment.
    struct ReviewChecks
    {
        static int Run(const std::filesystem::path& root, const std::string& mode)
        {
            namespace fs = std::filesystem;
            int failures = 0;
            auto expect = [&](bool ok, const char* what) {
                if (!ok)
                {
                    fprintf(stderr, "FAIL review %s: %s\n", mode.c_str(), what);
                    ++failures;
                }
            };
            auto set = [&](const char* name, const fs::path& value) { TestSetEnv(name, datadir::Text(value).c_str()); };
            fs::create_directories(root);
            for (const char* name : { "HOME", "CFFIXED_USER_HOME", "USERPROFILE" })
                set(name, root / "home");
            set("SKYLANDERS_DATA", root / "data");
            set("SKY_LAUNCHER_DATA", root / "data");
            set("SKY_LAUNCHER_INI", root / "data" / "launcher.ini");
            set("SKY_SETTINGS", root / "data" / "settings.ini");
            set("SKY_COLLECTION", root / "collection");
            set("SKY_SAVE_DIR", root / "data" / "SSA" / "saves");
            set("SKY_LAUNCHER_DESKTOP", root / "desktop");
            TestSetEnv("SKY_LAUNCHER_NO_UPDATE_CHECK", "1");
            TestSetEnv("SDL_VIDEO_DRIVER", "offscreen");
            TestSetEnv("SDL_AUDIO_DRIVER", "dummy");
            LauncherConfig config;
            config["launcher"]["language"] = "en";
            App app(*FindGame("ssa"), LauncherConfigPath(), config);
            if (mode == "paths")
            {
                const fs::path word = fs::path(u8"donn\u00e9es-\u30c7\u30fc\u30bf");
                const std::string original = collection::Create(14);
                expect(!original.empty(), "synthetic figure created");
                const fs::path figure = datadir::FromText(collection::Folder()) / fs::path(u8"\u30c7\u30fc\u30bf.sky");
                fs::rename(datadir::FromText(original), figure);
                CollectionModel model;
                model.gameId = "ssa";
                model.Load();
                expect(model.Find(datadir::Text(figure)) != nullptr, "collection signatures accept Unicode file names");
                const fs::path folder = root / word / "game";
                fs::create_directories(folder);
                std::ofstream(folder / "runtime.exe") << "stand-in";
                std::ofstream(folder / word) << "stand-in";
                config["install.synthetic"]["runtime"] = datadir::Text(folder / "runtime.exe");
                expect(UninstallGame({ folder, folder / "saves" }, config).ok, "Unicode game folder removed");
                expect(config.count("install.synthetic") == 0, "Unicode install record forgotten");
                uninstall::LauncherRemoval removal;
                removal.ok = true;
                removal.paths = { root / word / "a'b.exe" };
                const std::string script = uninstall::WindowsScript(removal, 1);
                expect(script.find(datadir::Text(root / word)) != std::string::npos,
                    "PowerShell receives UTF-8 path text");
                return failures == 0 ? 0 : 1;
            }
            if (mode == "data")
            {
                const fs::path game = root / "data" / "SSA", runtime = game / "runtime";
                fs::create_directories(game);
                std::ofstream(runtime) << "stand-in";
                const auto launch =
                    PrepareGameLaunch(*FindGame("ssa"), config, datadir::Text(game), datadir::Text(runtime));
                expect(launch.runtime.empty() && !launch.error.empty(),
                    "unwritable settings prevent starting with stale game settings");
                const auto result = DeleteGameData({ game, game });
                expect(!result.ok && fs::exists(runtime),
                    "a saves override naming the game folder cannot delete the game");
                fs::create_directories(root / "concurrent");
                const std::string file = datadir::Text(root / "concurrent" / "launcher.ini");
                std::atomic<int> refused{ 0 };
                std::vector<std::thread> writers;
                for (int writer = 0; writer < 4; ++writer)
                    writers.emplace_back([&, writer] {
                        LauncherConfig value;
                        value["launcher"]["payload"] = std::string(4096, char('a' + writer));
                        for (int write = 0; write < 10; ++write)
                            if (!SaveLauncherConfig(file, value))
                                ++refused;
                    });
                for (auto& writer : writers)
                    writer.join();
                const auto saved = LoadLauncherConfig(file);
                const std::string payload = SettingGet(saved, "launcher", "payload");
                expect(refused == 0 && payload.size() == 4096 &&
                           std::all_of(payload.begin(), payload.end(), [&](char c) { return c == payload.front(); }),
                    "concurrent settings writes each produce a complete file");
                return failures == 0 ? 0 : 1;
            }
            if (mode == "migration")
            {
                const std::string figure = collection::Create(14);
                config["portal"]["place"] = figure;
                const fs::path session = datadir::FromText(SessionFile(*collection::Catalogue::Get().FindGame("ssa")));
                fs::create_directories(session);
                std::ofstream(session / "keep") << "keep";
                app.m_s->collection->Open();
                expect(config["portal"]["place"] == figure && config["collection"]["moved"] != "1",
                    "failed migration retains the old portal settings for retry");
                return failures == 0 ? 0 : 1;
            }
            if (mode == "catalogue")
            {
                const fs::path file = datadir::FromText(collection::Folder()) / "catalogue.json";
                fs::create_directories(file.parent_path());
                const std::string damaged = "{\"format\":1,\"toys\":[";
                std::ofstream(file) << damaged;
                expect(!NameUnknownToy(3502, "Synthetic toy", collection::Category::Core, "ssa"),
                    "damaged catalogue refuses a rewrite");
                {
                    std::ifstream in(file);
                    expect(std::string(std::istreambuf_iterator<char>(in), {}) == damaged,
                        "damaged catalogue stays available for recovery");
                }
                fs::remove(file);
#if !defined(_WIN32)
                const std::string previous = "{\"format\":1,\"toys\":[]}";
                std::ofstream(file) << previous;
                rlimit limit{};
                expect(getrlimit(RLIMIT_FSIZE, &limit) == 0, "file-size limit read");
                const rlimit previousLimit = limit;
                limit.rlim_cur = 128;
                expect(setrlimit(RLIMIT_FSIZE, &limit) == 0, "write failure enabled");
                std::signal(SIGXFSZ, SIG_IGN);
                const bool saved = NameUnknownToy(3502, std::string(1024, 'x'), collection::Category::Core, "ssa");
                std::ifstream in(file);
                const std::string current(std::istreambuf_iterator<char>(in), {});
                setrlimit(RLIMIT_FSIZE, &previousLimit);
                expect(!saved, "failed catalogue write is reported");
                expect(current == previous, "failed catalogue rewrite preserves the previous names");
#else
                printf("SKIP catalogue short-write injection: requires a file-size limit\n");
#endif
                return failures == 0 ? 0 : 1;
            }
            if (mode == "uninstall")
            {
                fs::create_directories(root / "data" / "SSA");
                std::ofstream(root / "data" / "SSA" / "runtime") << "stand-in";
                app.OpenUninstall();
                savebackup::SetGameRunning(true, savebackup::Root("ssa"));
                app.RunUninstallEverything();
                expect(fs::exists(root / "data" / "SSA" / "runtime"), "a game started after the plan: nothing removed");
                expect(!app.m_uninstalled, "the running game refuses uninstall");
                savebackup::SetGameRunning(false, savebackup::Root("ssa"));
                return failures == 0 ? 0 : 1;
            }
            expect(app.Init(), "offscreen window initialized");
            if (failures != 0)
                return 1;
            i18n::SetLanguage("en");
            if (mode == "notes")
            {
                std::ofstream(root / "release.json")
                    << "{\"tag_name\":\"v99.0.0\",\"body\":\"#\\n###\\n# Title\",\"assets\":[]}";
                TestSetEnv("SKY_LAUNCHER_UPDATE_URL", TestFileUrl(root / "release.json").c_str());
                app.m_updates.Check({});
                while (app.m_updates.Busy())
                    SDL_Delay(5);
                expect(app.m_updates.Snapshot().checked, "local release read");
                app.m_whatsNewOpen = true;
                try
                {
                    app.Frame();
                    app.Frame();
                }
                catch (const std::out_of_range&)
                {
                    expect(false, "empty Markdown headings render without an exception");
                }
                return failures == 0 ? 0 : 1;
            }
            CollectionPage& page = *app.m_s->collection;
            app.m_page = Page::Collection;
            page.Open();
            std::vector<uint32_t> items;
            auto item = [&](const char* label) {
                for (uint32_t id : items)
                    if (ui::Ctx().testNavigationLabels[id] == label)
                        return id;
                return uint32_t(0);
            };
            ui::Ctx().testNavigationItems = &items;
            if (mode == "backups")
            {
                const std::string first = collection::Create(14), second = collection::Create(16);
                expect(!first.empty() && !second.empty(), "synthetic figures created");
                const std::string backup = collection::Backup(first);
                expect(!backup.empty(), "synthetic backup created");
                page.Reload();
                page.m_overlay = CollectionPage::Overlay::Manage;
                page.m_manageTab = 1;
                page.m_backupsOf = first;
                app.Frame();
                items.clear();
                app.Frame();
                const uint32_t restore = item("Restore");
                expect(restore != 0, "restore button submitted");
                if (restore != 0)
                {
                    ui::Ctx().testHeld = restore;
                    bool done = false;
                    ui::Ctx().hold = HoldConfirm{};
                    ui::Ctx().hold.Update(restore, true, true, SDL_GetTicks() + 1, done);
                    SDL_Delay(HoldConfirm::HoldMs + 5);
                    app.Frame(); // Restore reloads the collection while this list is being drawn.
                    expect(page.m_model.Find(first) != nullptr && page.m_model.Find(second) != nullptr,
                        "both figures remain after restore");
                }
            }
            else if (mode == "pictures")
            {
                // The pictures are read on the loader's thread (picture_loader.h): the page draws nothing in a picture's
                // place until it is ready, and a card already drawn never looks its picture up again.
                const fs::path portraits = root / "portraits";
                fs::create_directories(portraits);
                set("SKY_PORTRAITS", portraits);
                const collection::Toy* eruptor = collection::Catalogue::Get().Find(9);
                expect(eruptor != nullptr, "catalogue toy");
                if (eruptor == nullptr)
                    return 1;
                collection::Image square{ 4, 4, std::vector<uint32_t>(16, 0xFF336699u) };
                expect(collection::SavePng(square, datadir::Text(portraits / "eruptor.png")), "synthetic portrait");
                const size_t decoded = app.m_pictures.Decoded();
                const auto kind = collection::VariantKind::Base;
                const FigureIconTexture& asked = page.Icon(eruptor, kind, false, 0);
                expect(asked.pending && asked.texture == nullptr, "a picture is not read on the UI thread");
                std::thread::id worker;
                app.m_pictures.Request("check", [&] {
                    worker = std::this_thread::get_id();
                    return DecodedPicture{};
                });
                app.FinishPictures();
                expect(worker != std::thread::id() && worker != std::this_thread::get_id(),
                    "pictures are decoded on the loader's thread");
                const FigureIconTexture& ready = page.Icon(eruptor, kind, false, 0);
                expect(!ready.pending && ready.texture != nullptr && !ready.generated && ready.width == 4,
                    "the loader's picture becomes the card's texture");
                const size_t listed = collection::PictureFolderListings();
                bool same = true;
                for (int i = 0; i < 50; i++)
                    same = same && page.Icon(eruptor, kind, false, 0).texture == ready.texture;
                expect(same && app.m_pictures.Decoded() == decoded + 2 && !app.m_pictures.Pending() &&
                           collection::PictureFolderListings() == listed,
                    "a card already drawn neither reads nor looks up its picture again");
                page.ClearIcons(); // the disc's pictures arrived
                expect(page.Icon(eruptor, kind, false, 0).pending, "cleared pictures are read again");
                app.FinishPictures();
                expect(page.Icon(eruptor, kind, false, 0).texture != nullptr, "and come back");
                // A figure without a disc picture: its drawn icon, also from the loader.
                const collection::Toy* other = collection::Catalogue::Get().Find(14);
                expect(other != nullptr && page.Icon(other, kind, false, 0).pending, "drawn icons are queued too");
                app.FinishPictures();
                expect(
                    other != nullptr && page.Icon(other, kind, false, 0).generated, "a drawn icon without a picture");
                expect(FadeIn(0) == 1 && FadeIn(SDL_GetTicks()) < 0.1f && FadeIn(SDL_GetTicks() - 1000) == 1,
                    "pictures fade in once");
                // A logo cut to its opaque part and shrunk, off the UI thread (DecodeArt).
                collection::Image logo{ 8, 4, std::vector<uint32_t>(32, 0) };
                for (int y = 1; y < 3; y++)
                    for (int x = 2; x < 6; x++)
                        logo.rgba[size_t(y) * 8 + x] = 0xFF0000FFu;
                expect(collection::SavePng(logo, datadir::Text(root / "logo.png")), "synthetic logo");
                const DecodedPicture art = DecodeArt(datadir::Text(root / "logo.png"), true, 2);
                expect(
                    art.width == 2 && art.height == 1 && art.aspect == 2 && art.rgba.size() == 8 && art.rgba[3] == 255,
                    "art decoded, cropped and shrunk without a renderer");
                // The collection read again on a thread when its folder changes.
                const std::string made = collection::Create(16);
                const uint64_t later = SDL_GetTicks() + 1'000'000;
                expect(!page.m_model.PollInBackground(later) && page.m_model.Find(made) == nullptr,
                    "the reading starts on its own thread");
                page.m_model.WaitForReload();
                expect(page.m_model.PollInBackground(later) && page.m_model.Find(made) != nullptr,
                    "the new figure is swapped in");
                const std::string another = collection::Create(14);
                expect(!page.m_model.PollInBackground(later + 2000), "another reading starts");
                page.m_model.Load(); // the page reloaded meanwhile (a seat set, a figure deleted)
                page.m_model.WaitForReload();
                expect(!page.m_model.PollInBackground(later + 2000) && page.m_model.Find(another) != nullptr,
                    "a reading older than the last load is dropped");
            }
            else if (mode == "sheet")
            {
                const auto swapper = collection::CreateSwapper(2003, 1003);
                expect(!swapper.top.empty() && !swapper.bottom.empty(), "synthetic swapper created");
                page.Reload();
                page.OpenFigure(swapper.top);
                app.Frame();
                items.clear();
                app.Frame();
                const uint32_t favorite = item("Add to favorites");
                expect(favorite != 0, "favorite button submitted");
                ImGui::ActivateItemByID(favorite);
                app.Frame(); // The rest of the sheet still draws this swapper's bottom after the reload.
                const auto* figure = page.m_model.Find(swapper.top);
                expect(figure != nullptr && figure->favourite, "favorite action completed");
            }
            ui::Ctx().testHeld = 0;
            ui::Ctx().testNavigationItems = nullptr;
            return failures == 0 ? 0 : 1;
        }
    };
}
