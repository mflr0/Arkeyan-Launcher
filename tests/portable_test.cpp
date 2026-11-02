// "Export a portable copy" (docs/portable.md) on stand-ins: a fake installed game (a bundle on macOS, an executable elsewhere),
// a folder of disc files, a collection with a lock and a backups folder. The portable's pack holds every disc file, the keys, the
// figures but not the lock; the executable's own bytes stay; no room, a cancel and a second export (which replaces the first).
// Scratch folders only; no game data. Also --export-portable: the same export of a real installed game, for the owner's proof.
#include "test_env.h"
#include "launcher/game_install.h"
#include "launcher/game_keys.h"
#include "launcher/portable_export.h"
#include "shared/collection/datadir.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace
{
    int g_failures = 0;

#define EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL portable_test line %d: %s\n", __LINE__, #condition); g_failures++; } } while (0)

    namespace fs = std::filesystem;

    void Write(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    }

    std::string Read(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    }

    std::string EntryText(const fs::path& pack, const std::vector<portable::Entry>& entries, std::string_view name)
    {
        const portable::Entry* entry = portable::Find(entries, name);
        if (entry == nullptr)
            return "<none>";
        std::vector<uint8_t> bytes = portable::ReadEntry(pack, *entry);
        return std::string(bytes.begin(), bytes.end());
    }
}

int PortableChecks(const fs::path& scratch)
{
    g_failures = 0;
    const fs::path root = scratch / "portable";
    std::error_code ignored;
    fs::remove_all(root, ignored);

    // The disc's files (a copy of the game data is read the same way as an image), the installed game, the figures.
    const fs::path disc = root / "disc";
    Write(disc / "default.xex", std::string(5000, 'x'));
    Write(disc / "media" / "big.arc", std::string(5u << 20, 'b'));
    Write(disc / "media" / "empty.txt", "");
    const std::string program(300000, 'p');
#if defined(__APPLE__)
    const fs::path runtime = root / "game" / "SSARecomp.app" / "Contents" / "MacOS" / "SSARecomp";
    Write(root / "game" / "SSARecomp.app" / "Contents" / "Resources" / "fonts" / "a.ttf", "font");
#else
    const fs::path runtime = root / "game" / "SSARecomp.exe";
    Write(root / "game" / "THIRD_PARTY_NOTICES.md", "notices");
#endif
    Write(runtime, program);
    Write(root / "game" / "game_icon.png", "png");
    const fs::path collection = root / "collection";
    Write(collection / "Spyro.sky", std::string(1024, 's'));
    Write(collection / "Spyro.sky.lock", "123");
    Write(collection / "collection.json", "{}");
    Write(collection / "backups" / "Spyro.sky" / "old.sky", "old");

    LauncherConfig config;
    config["input"]["key_a"] = "K";
    PortableSpec spec;
    spec.gameId = "ssa";
    spec.runtime = runtime;
    spec.source = datadir::Text(disc);
    spec.collection = collection;
    spec.keys = PortableKeys(config, "ssa");
    spec.info = { "ssa", "0.9.5", "0.7.3", "abc", PortableStamp() };
    spec.folder = root / "out";
    spec.name = PortableName("ssa");
    spec.sign = false;
    EXPECT(spec.name == "Skylanders Spyro's Adventure");
    EXPECT(spec.keys.find("[ssa.input]\n") == 0 && spec.keys.find("key_a=K\n") != std::string::npos);
    EXPECT(spec.info.exported.size() == 15 && spec.info.exported[8] == '-');

    uint64_t lastDone = 0, lastTotal = 0;
    PortableResult result = ExportPortable(spec, nullptr, [&](uint64_t done, uint64_t total) { lastDone = done; lastTotal = total; });
    EXPECT(result.outcome == PortableResult::Outcome::Done);
    EXPECT(result.path == spec.folder / (spec.name + PortableExtension()) && fs::exists(result.path));
    EXPECT(lastTotal != 0 && lastDone == lastTotal);
#if defined(__APPLE__)
    const fs::path pack = result.path / "Contents" / "Resources" / portable::PackName;
    EXPECT(Read(result.path / "Contents" / "MacOS" / "SSARecomp") == program); // the signed executable is left as it is
    EXPECT(fs::exists(result.path / "Contents" / "Resources" / "fonts" / "a.ttf"));
#else
    const fs::path pack = result.path;
    EXPECT(Read(pack).compare(0, program.size(), program) == 0); // the executable's bytes, then the pack
#endif
    std::vector<portable::Entry> entries;
    std::string error;
    EXPECT(portable::ReadIndex(pack, entries, &error));
    for (const char* name : { "game/default.xex", "game/media/big.arc", "game/media/empty.txt", "portable.txt", "keys.ini", "icon.png",
             "collection/Spyro.sky", "collection/collection.json" })
        EXPECT(portable::Find(entries, name) != nullptr);
    EXPECT(portable::Find(entries, "collection/Spyro.sky.lock") == nullptr);
    EXPECT(portable::Find(entries, "game/media/big.arc")->size == (5u << 20));
    EXPECT(EntryText(pack, entries, "keys.ini") == spec.keys);
    EXPECT(portable::ParseInfo(EntryText(pack, entries, "portable.txt")).version == "0.9.5");
    EXPECT(EntryText(pack, entries, "icon.png") == "png");
    EXPECT(!fs::exists(spec.folder / ("." + spec.name + ".export")));
    const uint64_t firstSize = result.bytes;
    EXPECT(firstSize > (5u << 20));

    // Into the installed game's own folder: refused, the game is untouched.
    PortableSpec inside = spec;
    inside.folder = root / "game";
    result = ExportPortable(inside);
    EXPECT(result.outcome == PortableResult::Outcome::Failed && Read(runtime) == program);

    // No room: nothing is written.
    PortableSpec small = spec;
    small.freeBytes = 1000;
    small.folder = root / "out-small";
    result = ExportPortable(small);
    EXPECT(result.outcome == PortableResult::Outcome::NotEnoughSpace && result.needed > (5u << 20) && result.available == 1000);
    EXPECT(!PortableFailureText(result).empty() && !fs::exists(small.folder / (small.name + PortableExtension())));

    // Cancelled: nothing is left.
    std::atomic<bool> cancel{ true };
    PortableSpec cancelled = spec;
    cancelled.folder = root / "out-cancelled";
    result = ExportPortable(cancelled, &cancel);
    EXPECT(result.outcome == PortableResult::Outcome::Cancelled);
    EXPECT(fs::is_empty(cancelled.folder, ignored));

    // Exporting again replaces the copy: the new keys are in it.
    spec.keys = "[ssa.input]\nkey_a=L\nseeded=1\n";
    result = ExportPortable(spec);
    EXPECT(result.outcome == PortableResult::Outcome::Done && portable::ReadIndex(pack, entries, &error));
    EXPECT(EntryText(pack, entries, "keys.ini") == spec.keys);

    // A disc that is not there.
    spec.source = datadir::Text(root / "nothing.iso");
    result = ExportPortable(spec);
    EXPECT(result.outcome == PortableResult::Outcome::Failed && !result.error.empty());

    fs::remove_all(root, ignored);
    if (g_failures == 0)
        printf("portable copy: all checks passed\n");
    return g_failures;
}

// --export-portable <disc image or copy> <installed game's executable> <folder> [<collection folder>] [<launcher.ini>]
int ExportPortableTool(int argc, char** argv)
{
    if (argc < 5)
    {
        fprintf(stderr, "usage: ArkeyanLauncherTest --export-portable <disc> <game executable> <folder> [<collection>] [<launcher.ini>]\n");
        return 2;
    }
    LauncherConfig config = argc > 6 ? LoadLauncherConfig(argv[6]) : LauncherConfig();
    PortableSpec spec;
    spec.gameId = ActiveGame();
    if (const char* id = SDL_getenv("SKY_PORTABLE_GAME"))
        spec.gameId = id;
    spec.source = argv[2];
    spec.runtime = datadir::FromText(argv[3]);
    spec.folder = datadir::FromText(argv[4]);
    if (argc > 5)
        spec.collection = datadir::FromText(argv[5]);
    spec.keys = PortableKeys(config, spec.gameId);
    spec.info = { spec.gameId, SDL_getenv("SKY_PORTABLE_VERSION") != nullptr ? SDL_getenv("SKY_PORTABLE_VERSION") : "", "", "", PortableStamp() };
    spec.name = PortableName(spec.gameId);
    const auto start = std::chrono::steady_clock::now();
    PortableResult result = ExportPortable(spec);
    const long long seconds = (long long)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
    if (result.outcome != PortableResult::Outcome::Done)
    {
        fprintf(stderr, "export failed: %s\n", PortableFailureText(result).c_str());
        return 1;
    }
    printf("exported %s: %llu bytes in %lld s\n", datadir::Text(result.path).c_str(), (unsigned long long)result.bytes, seconds);
    return 0;
}
