#pragma once

#include "atomic_write_checks.h"
#include "launcher/portable_export.h"
#include "launcher/profiles.h"

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
        LauncherConfig pad;
        pad["input"] = { { "pad_wheel", "a" }, { "pad_toybox", "lb+rb" }, { "pad_menu", "lt+rt" }, { "pad_a", "b" },
            { "rumble_p1", "off" }, { "key_toybox", "F9" } };
        EXPECT(SaveLauncherConfig(ini.string(), pad));
        EXPECT(LoadLauncherConfig(ini.string()) == pad);
        const std::string portable = PortableKeys(pad, "ssa");
        EXPECT(portable.find("pad_wheel=") == std::string::npos && portable.find("pad_toybox=") == std::string::npos &&
               portable.find("pad_menu=") == std::string::npos);
        ResetPadMap(pad, "");
        EXPECT(pad["input"].count("pad_a") == 0 && pad["input"].count("rumble_p1") == 0);
        EXPECT(pad["input"].count("pad_wheel") == 0 && pad["input"].count("pad_toybox") == 0 &&
               pad["input"].count("pad_menu") == 0 && pad["input"]["key_toybox"] == "F9");
    }
    {
        LauncherConfig pad;
        const auto bindings = ToyboxPads();
        EXPECT(bindings.size() == 5);
        const std::array<int, 2> wheel = { 9, -1 }, toybox = { 9, 3 }, menu = { 9, 8 };
        EXPECT(ToyboxPadValue(pad, bindings[0]) == wheel && ToyboxPadValue(pad, bindings[1]) == toybox &&
               ToyboxPadValue(pad, bindings[2]) == menu);
        for (const ToyboxPad& binding : bindings)
        {
            EXPECT(!ToyboxPadConflict(pad, binding) && !IsPadRemapLine(binding.line));
            for (const char* invalid : { "", "unknown", "back+unknown", "back+back", "a+b+x", "+y", "a+" })
            {
                pad["input"][binding.line] = invalid;
                EXPECT(ToyboxPadValue(pad, binding) == binding.fallback);
            }
            // "none" unbinds a two-button shortcut; the wheel always has its button.
            pad["input"][binding.line] = "none";
            const std::array<int, 2> none = { -1, -1 };
            EXPECT(ToyboxPadValue(pad, binding) == (binding.buttons == 1 ? binding.fallback : none));
            pad["input"].erase(binding.line);
        }
        // Add and Clear: not bound by default, set like the Toy Box's, reset back to none.
        {
            const std::array<int, 2> none = { -1, -1 }, backB = { 9, 1 };
            EXPECT(std::string(bindings[3].line) == "pad_toybox_add" &&
                   std::string(bindings[4].line) == "pad_toybox_clear");
            EXPECT(ToyboxPadValue(pad, bindings[3]) == none && ToyboxPadValue(pad, bindings[4]) == none);
            EXPECT(ToyboxPadName(pad, bindings[4], PadFamily::Xbox360).empty());
            EXPECT(!ToyboxPadConflict(pad, bindings[4]) && !ToyboxPadOwn(pad, bindings[4]));
            SetToyboxPad(pad, bindings[4], { 9, 1 });
            EXPECT(pad["input"]["pad_toybox_clear"] == "back+b" && ToyboxPadValue(pad, bindings[4]) == backB);
            EXPECT(ToyboxPadOwn(pad, bindings[4]));
            EXPECT(ToyboxPadConflict(pad, bindings[4])); // B is a game's action
            ResetToyboxPad(pad, bindings[4]);
            EXPECT(pad["input"].count("pad_toybox_clear") == 0 && ToyboxPadValue(pad, bindings[4]) == none);
            const std::string portable = PortableKeys(pad, "ssa");
            EXPECT(portable.find("pad_toybox_add=") == std::string::npos);
        }
        pad["ssa.input"]["pad_wheel"] = "a"; // a game cannot override these physical buttons
        EXPECT(ToyboxPadValue(pad, bindings[0]) == wheel);
        SetToyboxPad(pad, bindings[0], { 0, -1 });
        SetToyboxPad(pad, bindings[1], { 4, 5 });
        SetToyboxPad(pad, bindings[2], { 6, 7 });
        EXPECT(pad["input"]["pad_wheel"] == "a" && pad["input"]["pad_toybox"] == "lb+rb" &&
               pad["input"]["pad_menu"] == "lt+rt");
        EXPECT(SaveLauncherConfig(ini.string(), pad));
        const auto loaded = LoadLauncherConfig(ini.string());
        for (const ToyboxPad& binding : bindings)
            EXPECT(ToyboxPadValue(loaded, binding) == ToyboxPadValue(pad, binding));
        LauncherConfig computer, personal;
        profiles::Split(pad, computer, personal);
        for (const ToyboxPad& binding : bindings)
            EXPECT(computer["input"][binding.line] == pad["input"][binding.line] &&
                   personal["input"].count(binding.line) == 0);
        EXPECT(ToyboxPadConflict(pad, bindings[0])); // every game's A is used
        SetPadTarget(pad, "ssa", 0, -1);
        EXPECT(ToyboxPadConflict(pad, bindings[0])); // another game still uses A
        for (const auto& game : g_games)
            SetPadTarget(pad, game.id, 0, -1);
        EXPECT(!ToyboxPadConflict(pad, bindings[0]));
        SetPadTarget(pad, "ssa", 0, 1);
        EXPECT(ToyboxPadConflict(pad, bindings[0])); // physical A still performs a game's action
        EXPECT(ToyboxPadName(pad, bindings[0], PadFamily::Xbox360) == "A");
        SetToyboxPad(pad, bindings[0], { 10, -1 });
        EXPECT(ToyboxPadConflict(pad, bindings[0])); // stick clicks also have controller rows
        SetToyboxPad(pad, bindings[0], { 0, -1 });
        SetToyboxPad(pad, bindings[1], { 9, 0 });
        EXPECT(ToyboxPadConflict(pad, bindings[1]));
        ResetPadMap(pad, "ssa");
        EXPECT(pad["input"]["pad_wheel"] == "a"); // a game's reset leaves the global shortcuts
        ResetPadMap(pad, "");
        for (const ToyboxPad& binding : bindings)
            EXPECT(ToyboxPadValue(pad, binding) == binding.fallback && !ToyboxPadConflict(pad, binding));
        pad["input"]["pad_wheel"] = "a";
        pad["ssa.input"]["pad_wheel"] = "a";
        EXPECT(MigrateInput(pad));
        EXPECT(pad["input"]["pad_wheel"] == "a" && pad["ssa.input"]["pad_wheel"] == "a");
        for (const char* button : PadButtons())
            EXPECT(IsPadRemapLine(std::string("pad_") + button));
        EXPECT(!IsPadRemapLine("pad_unknown"));
        for (int button = 0; button < PadButtonTotal; button++)
        {
            SetToyboxPad(pad, bindings[0], { button, -1 });
            EXPECT(ToyboxPadValue(pad, bindings[0])[0] == button);
            SetToyboxPad(pad, bindings[1], { button, (button + 1) % PadButtonTotal });
            EXPECT(ToyboxPadValue(pad, bindings[1])[0] == button &&
                   ToyboxPadValue(pad, bindings[1])[1] == (button + 1) % PadButtonTotal);
        }
    }
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
    ApplyGameVideo(config, "ssa");
    MigrateInput(config);
    EXPECT(config["video"]["vsync"] == "0" && config["ssa.input"]["key_a"] == "F2" && config["input"]["global"] == "1");
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
