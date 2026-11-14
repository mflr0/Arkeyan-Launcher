#pragma once

// Player profiles (docs/profiles.md): who plays. A profile has a pseudo, a picture, a colour and a stable id; it owns its Toy
// Box, its saves, its keys and game languages, its achievements and play time. The installed games, the discs, the pictures
// read from them, the video and audio settings and the launcher's own settings are the computer's, shared by every profile.
//
// The first profile is the data the launcher had before profiles: it stays where it is (no file moves) and keeps the Xbox
// id the games already saw, so its saves still open. Every other profile lives in <data>/profiles/<id>/.
#include "launcher/launcher.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace profiles
{
    constexpr size_t MaxName = 15; // an Xbox gamertag: what the games' name boxes hold

    struct Profile
    {
        std::string id;      // 16 hex digits, never reused; "first" for the profile that existed before profiles
        std::string name;    // the pseudo the games show
        int color = 2;       // one of the ten elements' colours (collection::Element order)
        std::string xuid;    // the offline Xbox id the games get, 16 hex digits
        bool hasPicture = false;
        std::string lastGame; // the catalogue id of the game it played last
        int64_t lastPlayed = 0; // seconds since 1970, 0: never

        bool First() const { return id == "first"; }
    };

    // The list, in the order the player chose, and the active profile.
    struct List
    {
        std::vector<Profile> profiles;
        std::string active;

        const Profile* Find(const std::string& id) const;
        Profile* Find(const std::string& id);
        const Profile& Active() const; // the active one, else the first
    };

    // <data>/profiles/: profiles.ini (the list) and one folder per profile but the first.
    std::filesystem::path Folder();
    // A profile's own folder: <data>/profiles/<id>/ (the first profile's holds only its picture).
    std::filesystem::path FolderOf(const Profile& profile);
    std::filesystem::path PictureOf(const Profile& profile); // picture.png in its folder (exists only when hasPicture)

    // The list as saved; on the first run, the first profile (named `firstName`, the launcher passes "" for "unnamed")
    // made from what is there.
    List Load(const std::string& firstName);
    bool Save(const List& list);

    // A new profile at the end of the list (not saved): a fresh id, its own Xbox id, an empty Toy Box.
    Profile& Add(List& list, const std::string& name, int color);
    // Removes a profile (never the last one) and moves its folder to `trash` when given (else deletes it). The active one
    // becomes the first left.
    bool Remove(List& list, const std::string& id, const std::filesystem::path& trash = {});

    // A pseudo the games can draw: Latin letters with French accents, digits, space, - _ . ' ; others are dropped, runs of
    // spaces folded, trimmed, cut to MaxName characters (not bytes). Empty when nothing is left.
    std::string CleanName(const std::string& name);
    // Initials for the drawn picture: the first letter of the first two words, or the first two letters.
    std::string Initials(const std::string& name);

    // The environment a profile gives the launcher and its games: its Toy Box (SKY_COLLECTION), its folder for the games'
    // player files (SKY_PROFILE_DIR, datadir::PlayerGame), its pseudo, picture and Xbox id (SKY_PLAYER_NAME, _PICTURE,
    // _XUID). The first profile sets only its name, picture and id: its Toy Box and saves are where they always were.
    // `player2`: the profile of the second pad, if any (SKY_PLAYER2_NAME, _PICTURE, _XUID).
    std::vector<std::pair<std::string, std::string>> Environment(
        const Profile& profile, const Profile* player2 = nullptr);

    // A key of the settings file that belongs to the player, not to the computer: a game's keys and pad ([<game>.input],
    // [<game>.pad]), its text and voice languages and its options ([<game>] language, voice_language, nightmare_from_start).
    bool PersonalKey(const std::string& section, const std::string& key);
    // Splits `config` into the computer's part and the profile's part (PersonalKey), and merges them back.
    void Split(const LauncherConfig& config, LauncherConfig& computer, LauncherConfig& personal);
    LauncherConfig Merge(const LauncherConfig& computer, const LauncherConfig& personal);
    // The profile's settings file (<folder>/profile.ini); the first profile keeps its keys in launcher.ini.
    std::filesystem::path SettingsOf(const Profile& profile);
}
