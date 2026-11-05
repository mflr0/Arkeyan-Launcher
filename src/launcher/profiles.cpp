#include "launcher/profiles.h"
#include "shared/collection/datadir.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <random>

namespace profiles
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr const char* FirstXuid = "E000000000000001"; // the id every game saw before profiles: its saves open

        std::string Get(const LauncherConfig& config, const std::string& section, const char* key)
        {
            auto found = config.find(section);
            if (found == config.end())
                return {};
            auto value = found->second.find(key);
            return value == found->second.end() ? std::string() : value->second;
        }

        std::string RandomHex()
        {
            std::random_device device;
            const uint64_t value = uint64_t(device()) << 32 | device();
            char text[17];
            snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(value));
            return text;
        }

        // The UTF-8 sequence starting at `at` and its length (1 for ASCII, 0 for a broken sequence).
        size_t SequenceLength(const std::string& text, size_t at)
        {
            const unsigned char lead = static_cast<unsigned char>(text[at]);
            const size_t length = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 0;
            if (length == 0 || at + length > text.size())
                return 0;
            for (size_t i = 1; i < length; i++)
                if ((static_cast<unsigned char>(text[at + i]) & 0xC0) != 0x80)
                    return 0;
            return length;
        }

        // The accented letters French writes, upper and lower case, in UTF-8: what the games' fonts draw besides ASCII.
        bool FrenchLetter(std::string_view sequence)
        {
            static constexpr std::string_view Letters =
                "àâäçéèêëîïôöùûüÿœæÀÂÄÇÉÈÊËÎÏÔÖÙÛÜŸŒÆ";
            for (size_t at = 0; at < Letters.size();)
            {
                const size_t length = (static_cast<unsigned char>(Letters[at]) >> 5) == 6 ? 2 : 1;
                if (Letters.substr(at, length) == sequence)
                    return true;
                at += length;
            }
            return false;
        }
    }

    const Profile* List::Find(const std::string& id) const
    {
        for (const Profile& profile : profiles)
            if (profile.id == id)
                return &profile;
        return nullptr;
    }

    Profile* List::Find(const std::string& id)
    {
        for (Profile& profile : profiles)
            if (profile.id == id)
                return &profile;
        return nullptr;
    }

    const Profile& List::Active() const
    {
        const Profile* found = Find(active);
        return found != nullptr ? *found : profiles.front();
    }

    fs::path Folder()
    {
        const fs::path root = datadir::Root();
        return root.empty() ? fs::path() : root / "profiles";
    }

    fs::path FolderOf(const Profile& profile)
    {
        const fs::path folder = Folder();
        return folder.empty() ? fs::path() : folder / profile.id;
    }

    fs::path PictureOf(const Profile& profile)
    {
        const fs::path folder = FolderOf(profile);
        return folder.empty() ? fs::path() : folder / "picture.png";
    }

    fs::path SettingsOf(const Profile& profile)
    {
        if (profile.First())
            return datadir::FromText(LauncherConfigPath());
        const fs::path folder = FolderOf(profile);
        return folder.empty() ? fs::path() : folder / "profile.ini";
    }

    List Load(const std::string& firstName)
    {
        List list;
        const fs::path file = Folder() / "profiles.ini";
        const LauncherConfig saved = Folder().empty() ? LauncherConfig() : LoadLauncherConfig(datadir::Text(file));
        // [profiles] order=<id>,<id>... active=<id>; one [profile.<id>] each.
        std::string order = Get(saved, "profiles", "order");
        for (size_t at = 0; at <= order.size() && !order.empty();)
        {
            const size_t comma = std::min(order.find(',', at), order.size());
            const std::string id = order.substr(at, comma - at);
            at = comma + 1;
            const std::string section = "profile." + id;
            if (id.empty() || saved.find(section) == saved.end() || list.Find(id) != nullptr)
                continue;
            Profile profile;
            profile.id = id;
            profile.name = CleanName(Get(saved, section, "name"));
            if (profile.name.empty())
                profile.name = firstName;
            profile.color = std::clamp(std::atoi(Get(saved, section, "color").c_str()), 0, 9);
            profile.xuid = Get(saved, section, "xuid");
            if (profile.xuid.size() != 16)
                profile.xuid = profile.First() ? FirstXuid : "E00000" + RandomHex().substr(6);
            profile.hasPicture = Get(saved, section, "picture") == "1";
            profile.lastGame = Get(saved, section, "last_game");
            profile.lastPlayed = std::atoll(Get(saved, section, "last_played").c_str());
            list.profiles.push_back(std::move(profile));
        }
        if (list.Find("first") == nullptr) // the data that was there before profiles, always a profile
        {
            Profile first;
            first.id = "first";
            first.name = firstName;
            first.xuid = FirstXuid;
            list.profiles.insert(list.profiles.begin(), first);
        }
        list.active = Get(saved, "profiles", "active");
        if (list.Find(list.active) == nullptr)
            list.active = list.profiles.front().id;
        return list;
    }

    bool Save(const List& list)
    {
        if (Folder().empty())
            return false;
        LauncherConfig config;
        std::string order;
        for (const Profile& profile : list.profiles)
        {
            order += (order.empty() ? "" : ",") + profile.id;
            auto& section = config["profile." + profile.id];
            section["name"] = profile.name;
            section["color"] = std::to_string(profile.color);
            section["xuid"] = profile.xuid;
            section["picture"] = profile.hasPicture ? "1" : "0";
            if (!profile.lastGame.empty())
                section["last_game"] = profile.lastGame;
            if (profile.lastPlayed != 0)
                section["last_played"] = std::to_string(profile.lastPlayed);
        }
        config["profiles"]["order"] = order;
        config["profiles"]["active"] = list.active;
        std::error_code error;
        fs::create_directories(Folder(), error);
        return SaveLauncherConfig(datadir::Text(Folder() / "profiles.ini"), config);
    }

    Profile& Add(List& list, const std::string& name, int color)
    {
        Profile profile;
        do
            profile.id = RandomHex();
        while (list.Find(profile.id) != nullptr || profile.id == "first");
        profile.name = CleanName(name);
        profile.color = std::clamp(color, 0, 9);
        // An offline id (0xE0...) of its own: a game keys its saves and achievements by it.
        profile.xuid = "E00000" + profile.id.substr(6);
        list.profiles.push_back(profile);
        return list.profiles.back();
    }

    bool Remove(List& list, const std::string& id, const fs::path& trash)
    {
        if (list.profiles.size() < 2 || list.Find(id) == nullptr)
            return false;
        const Profile gone = *list.Find(id);
        std::erase_if(list.profiles, [&](const Profile& profile) { return profile.id == id; });
        if (list.active == id)
            list.active = list.profiles.front().id;
        std::error_code error;
        const fs::path folder = FolderOf(gone);
        if (folder.empty() || !fs::exists(folder, error))
            return true;
        if (!trash.empty())
        {
            fs::create_directories(trash, error);
            fs::rename(folder, trash / (gone.name.empty() ? gone.id : gone.id + " " + gone.name), error);
            if (!error)
                return true;
        }
        fs::remove_all(folder, error);
        return !error;
    }

    std::string CleanName(const std::string& name)
    {
        std::string clean;
        size_t letters = 0;
        for (size_t at = 0; at < name.size() && letters < MaxName;)
        {
            const size_t length = SequenceLength(name, at);
            if (length == 0)
            {
                at++;
                continue;
            }
            const std::string_view sequence(name.data() + at, length);
            at += length;
            const char c = sequence[0];
            const bool ascii = length == 1 && (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '\'');
            if (length == 1 && (c == ' ' || c == '\t'))
            {
                if (!clean.empty() && clean.back() != ' ')
                    clean += ' ', letters++;
                continue;
            }
            if (ascii || FrenchLetter(sequence))
                clean += sequence, letters++;
        }
        while (!clean.empty() && clean.back() == ' ')
            clean.pop_back();
        return clean;
    }

    std::string Initials(const std::string& name)
    {
        // The first letter (one UTF-8 sequence) of each of the first two words, else the first two letters.
        std::vector<std::string> letters;
        bool wordStart = true;
        std::vector<std::string> all;
        for (size_t at = 0; at < name.size();)
        {
            const size_t length = std::max<size_t>(1, SequenceLength(name, at));
            const std::string sequence = name.substr(at, length);
            at += length;
            if (sequence == " ")
            {
                wordStart = true;
                continue;
            }
            all.push_back(sequence);
            if (wordStart && letters.size() < 2)
                letters.push_back(sequence);
            wordStart = false;
        }
        if (letters.size() < 2 && all.size() >= 2)
            letters = { all[0], all[1] };
        std::string initials;
        for (const std::string& letter : letters)
            initials += letter.size() == 1 ? std::string(1, char(std::toupper(static_cast<unsigned char>(letter[0])))) : letter;
        return initials;
    }

    std::vector<std::pair<std::string, std::string>> Environment(const Profile& profile, const Profile* player2)
    {
        std::vector<std::pair<std::string, std::string>> set;
        if (!profile.First())
        {
            const fs::path folder = FolderOf(profile);
            set.emplace_back("SKY_PROFILE_DIR", datadir::Text(folder));
            set.emplace_back("SKY_COLLECTION", datadir::Text(folder / "Collection"));
        }
        auto player = [&](const char* prefix, const Profile& who) {
            const std::string name(prefix);
            set.emplace_back(name + "_NAME", who.name);
            set.emplace_back(name + "_XUID", who.xuid);
            set.emplace_back(name + "_PICTURE", who.hasPicture ? datadir::Text(PictureOf(who)) : std::string());
        };
        player("SKY_PLAYER", profile);
        if (player2 != nullptr)
            player("SKY_PLAYER2", *player2);
        return set;
    }

    bool PersonalKey(const std::string& section, const std::string& key)
    {
        // A game's own sections: "<game>", "<game>.input", "<game>.pad" (and the computer's "<game>.video", "<game>.audio").
        const size_t dot = section.find('.');
        const std::string game = section.substr(0, dot), part = dot == std::string::npos ? "" : section.substr(dot + 1);
        if (FindGame(game) == nullptr)
            return false;
        if (part == "input" || part == "pad")
            return true;
        return part.empty() && (key == "language" || key == "voice_language" || key == "nightmare_from_start");
    }

    void Split(const LauncherConfig& config, LauncherConfig& computer, LauncherConfig& personal)
    {
        computer.clear();
        personal.clear();
        for (const auto& [section, keys] : config)
            for (const auto& [key, value] : keys)
                (PersonalKey(section, key) ? personal : computer)[section][key] = value;
    }

    LauncherConfig Merge(const LauncherConfig& computer, const LauncherConfig& personal)
    {
        LauncherConfig merged = computer;
        for (const auto& [section, keys] : personal)
            for (const auto& [key, value] : keys)
                if (PersonalKey(section, key))
                    merged[section][key] = value;
        return merged;
    }
}
