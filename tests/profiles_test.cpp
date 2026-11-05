// ArkeyanLauncherTest's checks for player profiles (launcher/profiles.h): the first profile from the data that was there, a
// new profile's own folder and Xbox id, the list saved and read back, removal, pseudos the games can draw, the environment a
// game gets and the split of the settings file between the computer and the player. On a scratch data folder; returns the
// number of failures.
#include "test_env.h"
#include "launcher/profiles.h"
#include "shared/collection/datadir.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace
{
    int g_failures = 0;

#define PROFILES_EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition); g_failures++; } } while (0)

    std::string Value(const std::vector<std::pair<std::string, std::string>>& set, const std::string& name)
    {
        for (const auto& [key, value] : set)
            if (key == name)
                return value;
        return "<unset>";
    }
}

int ProfilesChecks(const std::filesystem::path& scratch)
{
    g_failures = 0;
    namespace fs = std::filesystem;
    const fs::path root = scratch / "profiles-data";
    fs::remove_all(root);
    fs::create_directories(root);
    std::string oldData;
    const bool hadData = SDL_getenv_unsafe("SKYLANDERS_DATA") != nullptr;
    if (hadData)
        oldData = SDL_getenv_unsafe("SKYLANDERS_DATA");
    TestSetEnv("SKYLANDERS_DATA", root.string().c_str());

    // The first run: one profile, the data that was there, with the Xbox id every game already saw.
    profiles::List list = profiles::Load("Player 1");
    PROFILES_EXPECT(list.profiles.size() == 1 && list.profiles[0].First() && list.profiles[0].name == "Player 1" &&
                    list.profiles[0].xuid == "E000000000000001" && list.active == "first");
    const auto firstEnv = profiles::Environment(list.Active());
    PROFILES_EXPECT(Value(firstEnv, "SKY_PROFILE_DIR") == "<unset>" && Value(firstEnv, "SKY_COLLECTION") == "<unset>" &&
                    Value(firstEnv, "SKY_PLAYER_NAME") == "Player 1" && Value(firstEnv, "SKY_PLAYER_XUID") == "E000000000000001" &&
                    Value(firstEnv, "SKY_PLAYER_PICTURE").empty() && Value(firstEnv, "SKY_PLAYER2_NAME") == "<unset>");

    // A second profile: its own folder, Toy Box and Xbox id; the list kept in order with the active one.
    profiles::Profile& kaos = profiles::Add(list, "  Kaos   le  Grand ", 7);
    const std::string kaosId = kaos.id;
    PROFILES_EXPECT(kaos.name == "Kaos le Grand" && kaos.id.size() == 16 && kaos.xuid.size() == 16 && kaos.xuid.starts_with("E00000") &&
                    kaos.xuid != "E000000000000001" && kaos.color == 7);
    list.active = kaosId;
    list.Find(kaosId)->lastGame = "trapteam";
    PROFILES_EXPECT(profiles::Save(list));
    profiles::List again = profiles::Load("Player 1");
    PROFILES_EXPECT(again.profiles.size() == 2 && again.active == kaosId && again.profiles[1].name == "Kaos le Grand" &&
                    again.profiles[1].xuid == list.profiles[1].xuid && again.profiles[1].lastGame == "trapteam");
    const auto kaosEnv = profiles::Environment(again.Active(), &again.profiles[0]);
    const fs::path kaosFolder = profiles::FolderOf(again.Active());
    PROFILES_EXPECT(Value(kaosEnv, "SKY_PROFILE_DIR") == datadir::Text(kaosFolder) &&
                    Value(kaosEnv, "SKY_COLLECTION") == datadir::Text(kaosFolder / "Collection") &&
                    Value(kaosEnv, "SKY_PLAYER2_NAME") == "Player 1" && Value(kaosEnv, "SKY_PLAYER2_XUID") == "E000000000000001");

    // The games' player files follow the profile's folder (datadir::PlayerGame), the built game stays the computer's.
    TestSetEnv("SKY_PROFILE_DIR", datadir::Text(kaosFolder).c_str());
    PROFILES_EXPECT(datadir::PlayerGame("ssa") == kaosFolder / "SSA" && datadir::Game("ssa") == root / "SSA");
    TestUnsetEnv("SKY_PROFILE_DIR");
    PROFILES_EXPECT(datadir::PlayerGame("ssa") == root / "SSA");

    // Removal: never the last profile; the active one falls back to the first; the folder goes to the trash given.
    fs::create_directories(kaosFolder / "Collection");
    std::ofstream(kaosFolder / "Collection" / "x.sky") << "x";
    PROFILES_EXPECT(profiles::Remove(again, kaosId, scratch / "profiles-trash"));
    PROFILES_EXPECT(again.profiles.size() == 1 && again.active == "first" && !fs::exists(kaosFolder) &&
                    fs::exists(scratch / "profiles-trash" / (kaosId + " Kaos le Grand") / "Collection" / "x.sky"));
    PROFILES_EXPECT(!profiles::Remove(again, "first"));

    // Pseudos: what the games draw, cut to 15 letters, accents kept, the rest dropped.
    PROFILES_EXPECT(profiles::CleanName("Éloïse-Spyro_42") == "Éloïse-Spyro_42");
    PROFILES_EXPECT(profiles::CleanName("ab\xF0\x9F\x98\x80" "cd") == "abcd"); // an emoji
    PROFILES_EXPECT(profiles::CleanName("1234567890123456789") == "123456789012345");
    PROFILES_EXPECT(profiles::CleanName("   ").empty() && profiles::CleanName("a\xC3") == "a");
    PROFILES_EXPECT(profiles::Initials("spyro le dragon") == "SL" && profiles::Initials("Kaos") == "KA" &&
                    profiles::Initials("élodie") == "éL");

    // The settings file: a game's keys, pad and languages are the player's; its video and disc are the computer's.
    LauncherConfig config;
    config["launcher"]["language"] = "fr";
    config["ssa"]["iso"] = "/discs/ssa.iso";
    config["ssa"]["language"] = "de";
    config["ssa.input"]["key_jump"] = "space";
    config["ssa.video"]["fps_limit"] = "60";
    config["swapforce"]["nightmare_from_start"] = "1";
    LauncherConfig computer, personal;
    profiles::Split(config, computer, personal);
    PROFILES_EXPECT(computer["launcher"]["language"] == "fr" && computer["ssa"]["iso"] == "/discs/ssa.iso" &&
                    computer["ssa.video"]["fps_limit"] == "60" && !computer["ssa"].contains("language") && !computer.contains("ssa.input"));
    PROFILES_EXPECT(personal["ssa"]["language"] == "de" && personal["ssa.input"]["key_jump"] == "space" &&
                    personal["swapforce"]["nightmare_from_start"] == "1" && !personal.contains("launcher"));
    PROFILES_EXPECT(profiles::Merge(computer, personal) == config);

    if (hadData)
        TestSetEnv("SKYLANDERS_DATA", oldData.c_str());
    else
        TestUnsetEnv("SKYLANDERS_DATA");
    printf("Profiles: the first from what was there, a new one's folder and id, saved, removed, pseudos, environment, settings split: %d failures\n",
        g_failures);
    return g_failures;
}
