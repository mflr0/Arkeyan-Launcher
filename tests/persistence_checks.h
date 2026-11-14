#pragma once

#include "atomic_write_checks.h"

inline void ConfigPersistenceChecks(const std::filesystem::path& scratch)
{
    namespace fs = std::filesystem;
    const fs::path root = scratch / "config-persistence";
    fs::create_directories(root);
    auto read = [](const fs::path& file) {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    const fs::path ini = root / "launcher.ini";
    {
        std::ofstream(ini, std::ios::binary)
            << "\xEF\xBB\xBF# edited\r\nfuture_root=keep=equals;#text\r\n"
               "[future.section]\r\nfeature=enabled\r\n[ssa.video]\r\nvsync=0\r\nfuture_video=opaque\r\n"
               "[ssa.input]\r\nkey_a=F2\r\nfuture_input=opaque\r\n[giants.video]\r\nvsync=1\r\n"
               "[giants.input]\r\nkey_a=F3\r\n[install.synthetic]\r\nruntime=missing\r\nkit=old\r\nfuture_install="
               "keep\r\n";
    }
    LauncherConfig config = LoadLauncherConfig(ini.string());
    EXPECT(config[""]["future_root"] == "keep=equals;#text" && config["future.section"]["feature"] == "enabled");
    SeedGameVideo(config, "ssa");
    SeedGameInput(config, "ssa");
    ApplyGameVideo(config, "ssa");
    ApplyGameInput(config, "ssa");
    EXPECT(config["video"]["vsync"] == "0" && config["input"]["key_a"] == "F2");
    config["video"]["vsync"] = "1";
    StoreGameVideo(config, "ssa");
    RecordInstalledGame(config, "synthetic", "synthetic-runtime", "new", "1.2.3");
    EXPECT(SaveLauncherConfig(ini.string(), config));
    const auto saved = LoadLauncherConfig(ini.string());
    EXPECT(saved.at("ssa.video").at("vsync") == "1" && saved.at("video").at("vsync") == "1");
    EXPECT(saved == config && saved.at("ssa.video").at("future_video") == "opaque");
    EXPECT(saved.at("ssa.input").at("future_input") == "opaque" &&
           saved.at("install.synthetic").at("future_install") == "keep");
    EXPECT(saved.at("giants.video").at("vsync") == "1" && saved.at("giants.input").at("key_a") == "F3");

    const std::string before = read(ini);
    fs::create_directories(root / "launcher.ini.tmp");
    {
        std::ofstream(root / "launcher.ini.tmp" / "keep") << "blocked staging";
    }
    config["ssa.video"]["vsync"] = "0";
    bool written = true;
    EXPECT(WithBlockedFileWrite(ini, [&] { written = SaveLauncherConfig(ini.string(), config); }));
    EXPECT(!written && read(ini) == before && LoadLauncherConfig(ini.string()) == saved && NoAtomicTemporary(ini));
    EXPECT(SaveLauncherConfig(ini.string(), config) && LoadLauncherConfig(ini.string()) == config);
    EXPECT(read(root / "launcher.ini.tmp" / "keep") == "blocked staging");
    fs::remove_all(root / "launcher.ini.tmp");
    EXPECT(!fs::exists(root / "launcher.ini.tmp"));
    EXPECT(NoAtomicTemporary(ini));

    // The player's comments in the file survive a save (issue #9, S-27): above the section or the key they were written over, the
    // file's first ones at the top, the last ones at the end; the comment over a key that goes goes with it.
    {
        const fs::path commented = root / "commented.ini";
        {
            std::ofstream(commented, std::ios::binary)
                << "\xEF\xBB\xBF# my notes on this file\n[launcher]\n# keep my language\nlanguage=fr\n; old "
                   "idea\nfoo=bar\n"
                   "\n# the video of Spyro\n[ssa.video]\n# quiet\nvsync=0\n# end of file\n";
        }
        LauncherConfig noted = LoadLauncherConfig(commented.string());
        EXPECT(noted["launcher"]["language"] == "fr" && noted["ssa.video"]["vsync"] == "0");
        noted["launcher"]["language"] = "en";
        noted["launcher"].erase("foo");
        EXPECT(SaveLauncherConfig(commented.string(), noted));
        const std::string text = read(commented);
        const auto at = [&](const char* needle) { return text.find(needle); };
        EXPECT(at("# my notes on this file") != std::string::npos && at("# my notes on this file") < at("[launcher]"));
        EXPECT(at("# keep my language") != std::string::npos && at("# keep my language") < at("language=en") &&
               at("[launcher]") < at("# keep my language"));
        EXPECT(at("old idea") == std::string::npos && at("foo=") == std::string::npos);
        EXPECT(at("# the video of Spyro") < at("[ssa.video]") && at("# quiet") < at("vsync=0") &&
               at("[ssa.video]") < at("# quiet"));
        EXPECT(at("# end of file") != std::string::npos && at("vsync=0") < at("# end of file"));
        EXPECT(LoadLauncherConfig(commented.string()) == noted);
        const std::string once = text;
        EXPECT(
            SaveLauncherConfig(commented.string(), noted) && read(commented) == once); // saving again changes nothing
        EXPECT(text.find("# Launcher settings: one [section] per game.") ==
               text.rfind("# Launcher settings: one [section] per game."));
    }

    // A failed final rename must also leave its target intact and remove the staging file.
    fs::create_directories(root / "target.ini");
    {
        std::ofstream(root / "target.ini" / "keep") << "target sentinel";
    }
    EXPECT(!SaveLauncherConfig((root / "target.ini").string(), config));
    EXPECT(read(root / "target.ini" / "keep") == "target sentinel" && NoAtomicTemporary(root / "target.ini"));
    printf("Config persistence: unknown keys, game contracts, failed writes and rename recovery checked\n");
}
