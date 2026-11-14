#include "test_env.h"
#include "shared/art/game_text.h"
#include "shared/art/alchemy_archive.h"
#include "shared/art/lux_images.h"
#include "shared/collection/json.h"
#include "shared/vfs.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>

namespace
{
    int g_failures = 0;
#define LEVEL_CHECK(condition)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("FAIL level names line %d: %s\n", __LINE__, #condition);                                            \
            g_failures++;                                                                                              \
        }                                                                                                              \
    } while (0)

    using Bytes = std::vector<uint8_t>;
    using Files = std::map<std::string, Bytes>;
    using Json = collection::Json;

    Json Parse(const std::string& text)
    {
        Json json;
        std::string error;
        LEVEL_CHECK(Json::Parse(text, json, error));
        return json;
    }

    void Put(Bytes& bytes, size_t at, uint32_t value)
    {
        if (bytes.size() < at + 4)
            bytes.resize(at + 4);
        for (int i = 0; i < 4; i++)
            bytes[at + size_t(i)] = uint8_t(value >> (24 - 8 * i));
    }

    art::Disc MemoryDisc(const Files& files)
    {
        return { [&files](const std::string& path) -> uint64_t {
                    auto it = files.find(path);
                    return it == files.end() ? 0 : it->second.size();
                },
            [&files](const std::string& path, uint64_t at, size_t size, uint8_t* out) {
                auto it = files.find(path);
                if (it == files.end() || at > it->second.size() || size > it->second.size() - at)
                    return false;
                memcpy(out, it->second.data() + at, size);
                return true;
            },
            [&files]() {
                std::vector<std::string> names;
                for (const auto& [path, bytes] : files)
                    names.push_back(path);
                return names;
            } };
    }

    // An uncompressed v10 archive, populated entirely with invented metadata and text.
    Bytes Archive(const Files& entries)
    {
        Bytes out(0x800, 0), names(entries.size() * 4, 0);
        memcpy(out.data(),
            "\x1a"
            "AGI",
            4);
        Put(out, 4, 10);
        Put(out, 12, uint32_t(entries.size()));
        size_t i = 0;
        for (const auto& [name, bytes] : entries)
        {
            Put(names, i * 4, uint32_t(names.size()));
            names.insert(names.end(), name.begin(), name.end());
            names.push_back(0);
            size_t table = 0x38 + entries.size() * 4 + i++ * 16;
            Put(out, table + 4, uint32_t(out.size()));
            Put(out, table + 8, uint32_t(bytes.size()));
            Put(out, table + 12, 0xFFFFFFFF);
            out.insert(out.end(), bytes.begin(), bytes.end());
        }
        Put(out, 0x2C, uint32_t(out.size()));
        Put(out, 0x30, uint32_t(names.size()));
        out.insert(out.end(), names.begin(), names.end());
        return out;
    }

    Bytes TextBytes(const std::string& text) { return { text.begin(), text.end() }; }

    struct Igz
    {
        Bytes pool = Bytes(32, 0);
        std::vector<std::string> types;
        std::vector<uint32_t> objects;
        std::map<std::string, std::pair<uint32_t, Bytes>> records;

        uint32_t Data(Bytes bytes)
        {
            uint32_t at = uint32_t(pool.size());
            pool.insert(pool.end(), bytes.begin(), bytes.end());
            while (pool.size() % 4)
                pool.push_back(0);
            return at;
        }
        uint32_t Text(const std::string& text)
        {
            Bytes bytes(text.begin(), text.end());
            bytes.push_back(0);
            return Data(std::move(bytes));
        }
        uint32_t Object(const std::string& type, size_t words = 16)
        {
            auto at = std::find(types.begin(), types.end(), type);
            uint32_t index = uint32_t(at - types.begin());
            if (at == types.end())
                types.push_back(type);
            uint32_t object = Data(Bytes(words * 4, 0));
            Put(pool, object, index);
            objects.push_back(object);
            return object;
        }
        void Word(uint32_t object, int word, uint32_t value) { Put(pool, object + word * 4, value); }
        void Record(const std::string& tag, uint32_t count, Bytes bytes) { records[tag] = { count, std::move(bytes) }; }
        Bytes Words(std::initializer_list<uint32_t> words)
        {
            Bytes bytes;
            for (uint32_t word : words)
                Put(bytes, bytes.size(), word);
            return bytes;
        }
        uint32_t List(const std::string& type, std::initializer_list<uint32_t> members)
        {
            uint32_t list = Object(type);
            Word(list, 2, uint32_t(members.size()));
            Word(list, 5, Data(Words(members)));
            return list;
        }
        uint32_t Info(const std::string& name)
        {
            uint32_t info = Object("StringInfo");
            Word(info, 2, Text(name));
            return info;
        }
        void Set(const std::string& name, std::initializer_list<uint32_t> members)
        {
            uint32_t set = Object("ScriptSet");
            Word(set, 2, Text(name));
            Word(set, 6, List("ScriptSetList", members));
        }
        Bytes Finish()
        {
            Bytes names;
            for (const std::string& type : types)
            {
                names.insert(names.end(), type.begin(), type.end());
                names.push_back(0);
                if (names.size() % 2)
                    names.push_back(0);
            }
            Record("TMET", uint32_t(types.size()), names);
            Bytes packed;
            size_t nibble = 0;
            uint32_t previous = 0;
            for (uint32_t object : objects)
            {
                uint32_t delta = (object - previous) / 4;
                do
                {
                    uint8_t value = uint8_t((delta & 7) | (delta > 7 ? 8 : 0));
                    if (nibble % 2 == 0)
                        packed.push_back(value);
                    else
                        packed.back() |= uint8_t(value << 4);
                    nibble++;
                    delta >>= 3;
                } while (delta);
                previous = object;
            }
            Record("RVTB", uint32_t(objects.size()), packed);
            Bytes fixups;
            for (const auto& [tag, record] : records)
            {
                size_t start = fixups.size();
                Put(fixups, start,
                    uint32_t(uint8_t(tag[3])) << 24 | uint32_t(uint8_t(tag[2])) << 16 | uint32_t(uint8_t(tag[1])) << 8 |
                        uint8_t(tag[0]));
                Put(fixups, start + 4, record.first);
                Put(fixups, start + 8, uint32_t(record.second.size() + 16));
                Put(fixups, start + 12, 16);
                fixups.insert(fixups.end(), record.second.begin(), record.second.end());
            }
            Bytes out(0x1000, 0);
            memcpy(out.data(), "IGZ\x01", 4);
            Put(out, 4, 9);
            Put(out, 0x18, 0x800);
            Put(out, 0x1C, uint32_t(fixups.size()));
            Put(out, 0x28, 0x1000);
            Put(out, 0x2C, uint32_t(pool.size()));
            LEVEL_CHECK(fixups.size() <= 0x800);
            std::copy(fixups.begin(), fixups.end(), out.begin() + 0x800);
            out.insert(out.end(), pool.begin(), pool.end());
            return out;
        }
    };

    Bytes Pack(const std::vector<std::u16string>& texts)
    {
        Igz file;
        uint32_t info = file.Object("LanguagePackInfo");
        Bytes data;
        for (const auto& text : texts)
        {
            for (char16_t c : text)
            {
                data.push_back(uint8_t(c >> 8));
                data.push_back(uint8_t(c));
            }
            data.push_back(0);
            data.push_back(0);
        }
        file.Word(info, 6, uint32_t(data.size()));
        file.Word(info, 7, file.Data(data));
        return file.Finish();
    }

    void ScriptChecks()
    {
        Igz file;
        uint32_t load = file.Info("load"), title = file.Info("title"), unnamed = file.Info("unnamed");
        file.Info("challengeBase");
        file.Info("Challenge_Title_991");
        file.Info("hubPrefix");
        file.Info("hubTitle");
        file.Set("loads", { load, unnamed });
        file.Set("shortTitles", { title });
        file.Set("titles", { title, unnamed });
        Files entries{ { "level.bld", file.Finish() },
            { "FRENCH.pak", Pack({ u"Level_999_InventedTypo", u"Forêt inventée", u"", u"Challenge_Level_",
                                u"Invented challenge", u"Level_Hub_Invented_0", u"Invented hub 🌟" }) } };
        Files disc{ { "global.bld", Archive(entries) }, { "level/level_999_inventedtypo.bld", { 0 } },
            { "level/challenge_level_991.bld", { 0 } }, { "level/level_hub_invented_01.bld", { 0 } },
            { "level/level_hub_invented_02.bld", { 0 } }, { "level/level_hub_invented_0music.bld", { 0 } } };
        Json manifest = Parse(R"({"kind":"script","archive":"global.bld","entry":"level.bld",
            "languages":{"en":"ENGLISH","fr":"FRENCH"},"sets":[{"loads":"loads","titles":"titles"}],
            "numbered":[{"load":"challengeBase","titlePrefix":"Challenge_Title_"}],
            "prefixes":[{"load":"hubPrefix","title":"hubTitle"}]})");
        Json levels = art::ReadLevelNamesFrom(manifest, MemoryDisc(disc));
        LEVEL_CHECK(levels.Members().size() == 4);
        LEVEL_CHECK(levels["level_999_inventedtypo"]["fr"].AsString() == "Forêt inventée");
        LEVEL_CHECK(!levels["level_999_inventedtypo"].Has("en"));
        LEVEL_CHECK(levels["challenge_level_991"]["fr"].AsString() == "Invented challenge");
        LEVEL_CHECK(levels["level_hub_invented_01"]["fr"].AsString() == "Invented hub 🌟");
        LEVEL_CHECK(levels.Has("level_hub_invented_02") && !levels.Has("level_hub_invented_0music"));
        disc.erase("level/level_999_inventedtypo.bld");
        LEVEL_CHECK(!art::ReadLevelNamesFrom(manifest, MemoryDisc(disc)).Has("level_999_inventedtypo"));
        manifest = Parse(R"({"kind":"script","archive":"global.bld","entry":"level.bld",
            "languages":{"fr":"FRENCH"},"sets":[{"loads":"loads","titles":"shortTitles"}]})");
        LEVEL_CHECK(art::ReadLevelNamesFrom(manifest, MemoryDisc(disc)).Members().empty());
    }

    void IgzChecks()
    {
        Igz zone;
        uint32_t object = zone.Object("CZoneInfo");
        zone.Word(object, 2, zone.Text("Invented/Zone/Room"));
        uint32_t root = zone.List("igObjectList", { object });
        uint32_t label = zone.Text("Zone");
        uint32_t names = zone.List("igNameList", { label, 0 });
        zone.Word(names, 2, 1);
        zone.Record("ROOT", 1, zone.Words({ root }));
        zone.Record("ONAM", 1, zone.Words({ names }));
        Igz language;
        uint32_t text = language.Object("igLocalizedStringData");
        language.Word(text, 2, language.Text("Invented zone"));
        language.Word(text, 3, 0x80000000);
        language.Word(text, 4, 0x80000000);
        language.Record("EXNM", 2, language.Words({ 0x80000000, 0, 0, 1 }));
        Bytes strings = TextBytes(std::string("Zone\0\0CZoneInfo::_displayName\0", 30));
        language.Record("TSTR", 2, strings);
        Files files{ { "zones.pak",
            Archive({ { "room_zoneinfo.igz", zone.Finish() }, { "room_zoneInfo_en.lng", language.Finish() } }) } };
        Json manifest = Parse(R"({"kind":"igz","archive":"zones.pak"})");
        Json levels = art::ReadLevelNamesFrom(manifest, MemoryDisc(files));
        LEVEL_CHECK(levels.Members().size() == 1);
        LEVEL_CHECK(levels["invented/zone/room"]["en"].AsString() == "Invented zone");
        language.Word(text, 3, 0x80000001);
        files["zones.pak"] =
            Archive({ { "room_zoneinfo.igz", zone.Finish() }, { "room_zoneinfo_en.lng", language.Finish() } });
        LEVEL_CHECK(art::ReadLevelNamesFrom(manifest, MemoryDisc(files)).Members().empty());
    }

    uint32_t Crc(const std::string& text)
    {
        uint32_t crc = 0xFFFFFFFF;
        for (uint8_t c : text)
        {
            crc ^= c;
            for (int bit = 0; bit < 8; bit++)
                crc = crc >> 1 ^ (crc & 1 ? 0xEDB88320 : 0);
        }
        return ~crc;
    }

    // A stored DEFLATE block: also exercises the raw chunk path without using a compressor.
    Bytes Deflate(const Bytes& bytes)
    {
        uint16_t length = uint16_t(bytes.size()), inverse = uint16_t(~length);
        Bytes out{ 1, uint8_t(length), uint8_t(length >> 8), uint8_t(inverse), uint8_t(inverse >> 8) };
        out.insert(out.end(), bytes.begin(), bytes.end());
        return out;
    }

    void DeflateArchiveChecks()
    {
        // A fixed Huffman stream of one thousand invented 'x' bytes.
        Bytes body{ 11, 0, 171, 168, 24, 5, 163, 96, 20, 12, 119, 0, 0 };
        Bytes bytes = Archive({ { "invented", body } });
        Put(bytes, 4, 11);
        Put(bytes, 0x44, 1000);
        Put(bytes, 0x48, 0x10000000);
        auto open = [&]() {
            return art::alchemy::IgaArchive::Open(
                [&](uint64_t at, size_t size, uint8_t* out) {
                    if (at > bytes.size() || size > bytes.size() - at)
                        return false;
                    memcpy(out, bytes.data() + at, size);
                    return true;
                },
                bytes.size());
        };
        auto archive = open();
        LEVEL_CHECK(archive.has_value());
        if (!archive)
            return;
        LEVEL_CHECK(archive->Read(0) == Bytes(1000, 'x'));
        LEVEL_CHECK(archive->Read(0, 4) == Bytes(4, 'x'));
        // The reader must reject a truncated compressed body, even if the missing bytes would be zero.
        bytes.resize(0x800 + body.size() - 1);
        archive = open();
        LEVEL_CHECK(archive && archive->Read(0).empty());
    }

    void LuxChecks()
    {
        constexpr char Ids[] = "Level_999\0PvP_Level_999\0Challenge_Level_999\0";
        Bytes identifiers(Ids, Ids + sizeof(Ids) - 1);
        Bytes stream(20, 0);
        memcpy(stream.data(), "strm", 4);
        stream.push_back(0x78);
        stream.push_back(0x01);
        Bytes compressed = Deflate(identifiers);
        stream.insert(stream.end(), compressed.begin(), compressed.end());
        Bytes table(0x90, 0);
        Put(table, 0x7C, 3);
        table.resize(0x98, 0);
        Put(table, 0x80, Crc("Level_999"));
        Put(table, 0x84, uint32_t(table.size() - 0x84));
        Bytes name = TextBytes("Imaginary hillside");
        name.push_back(0);
        table.insert(table.end(), name.begin(), name.end());
        Put(table, 0x88, Crc("PvP_Level_999"));
        Put(table, 0x8C, uint32_t(table.size() - 0x8C));
        table.push_back(0); // an empty translation is omitted
        Put(table, 0x90, Crc("Challenge_Title_999"));
        Put(table, 0x94, uint32_t(table.size() - 0x94));
        Bytes challenge = TextBytes("Invented heroic title");
        table.insert(table.end(), challenge.begin(), challenge.end());
        table.push_back(0);
        Files files{ { "global.str", stream }, { "text/eng/global.lxb", table } };
        Json manifest = Parse(R"({"kind":"lux","stream":"global.str","text":"text/{language}/global.lxb",
            "languages":{"en":"eng","fr":"fre"},"challengePrefix":"Challenge_Title_"})");
        Json levels = art::ReadLevelNamesFrom(manifest, MemoryDisc(files));
        LEVEL_CHECK(levels.Members().size() == 2);
        LEVEL_CHECK(levels["level_999"]["en"].AsString() == "Imaginary hillside");
        LEVEL_CHECK(
            !levels.Has("pvp_level_999") && levels["challenge_level_999"]["en"].AsString() == "Invented heroic title");
        files["text/eng/global.lxb"].resize(0x86);
        LEVEL_CHECK(art::ReadLevelNamesFrom(manifest, MemoryDisc(files)).Members().empty());
        Bytes raw;
        LEVEL_CHECK(art::lux::InflateRaw(compressed.data(), compressed.size(), raw, identifiers.size()));
        LEVEL_CHECK(raw == identifiers);
        raw.clear();
        LEVEL_CHECK(!art::lux::InflateRaw(compressed.data(), compressed.size() - 1, raw));
        raw.clear();
        LEVEL_CHECK(!art::lux::InflateRaw(compressed.data(), compressed.size(), raw, identifiers.size() - 1));
    }

    void MuaChecks()
    {
        const std::string zone = "<igx><object type='CZoneInfo' refname='Zone'>"
                                 "<var name='_zoneName' value='Invented/Room'/></object></igx>";
        const std::string localized = "<igx><!-- invented --> <object type='igLocalizedString'>"
                                      "<var name='_object' ref='room_zoneinfo.Zone'/>"
                                      "<var name='_field' ref='metafield.CZoneInfo::_displayName'/>"
                                      "<var name='_string' value='A &amp; B &lt; C > D &#x1F31F;'/></object>"
                                      "<object type='igLocalizedString'><var name='_object' ref='room_zoneinfo.Other'/>"
                                      "<var name='_field' ref='metafield.CZoneInfo::_displayName'/>"
                                      "<var name='_string' value='Wrong object'/></object></igx>";
        Files discFiles{ { "zones.pak", Archive({ { "folder/room_zoneinfo.mua", TextBytes(zone) },
                                            { "folder/room_zoneinfo_en.mua", TextBytes(localized) },
                                            { "folder/room_zoneinfo_fr.mua", TextBytes("<igx/>") } }) } };
        Json manifest = Parse(R"({"kind":"mua","archive":"zones.pak"})");
        Json levels = art::ReadLevelNamesFrom(manifest, MemoryDisc(discFiles));
        LEVEL_CHECK(levels.Members().size() == 1);
        LEVEL_CHECK(levels["invented/room"]["en"].AsString() == "A & B < C > D \xF0\x9F\x8C\x9F");
        LEVEL_CHECK(!levels["invented/room"].Has("fr"));
        LEVEL_CHECK(!levels.Has("room"));
        discFiles["zones.pak"] = Archive({ { "room_zoneinfo.mua", TextBytes(zone) },
            { "room_zoneinfo_en.mua", TextBytes("<object type='igLocalizedString'><var value='cut") } });
        LEVEL_CHECK(art::ReadLevelNamesFrom(manifest, MemoryDisc(discFiles)).Members().empty());
    }

    void CacheChecks(const std::filesystem::path& root)
    {
        std::filesystem::create_directories(root);
        const std::string old = getenv("SKYLANDERS_DATA") ? getenv("SKYLANDERS_DATA") : "";
        TestSetEnv("SKYLANDERS_DATA", root.generic_string().c_str());
        const Files empty;
        auto disc = MemoryDisc(empty);
        LEVEL_CHECK(art::Extract("ssa", disc, {}, art::LevelNames).levelNames);
        const auto file = root / "SSA" / "levels.json";
        LEVEL_CHECK(std::filesystem::is_regular_file(file));
        // Each read closes its file: Windows cannot replace a file that is still open.
        auto read = [](const std::filesystem::path& path) {
            std::ifstream input(path);
            return Parse(std::string(std::istreambuf_iterator<char>(input), {}));
        };
        Json cached = read(file);
        LEVEL_CHECK(cached["reader"].AsInt() == art::LevelNamesVersion && cached["levels"].Members().empty());
        // An empty current cache is a completed read; missing levels retain the presence fallback.
        art::Disc unreadable{ [](const std::string&) -> uint64_t {
                                 LEVEL_CHECK(false);
                                 return 0;
                             },
            [](const std::string&, uint64_t, size_t, uint8_t*) {
                LEVEL_CHECK(false);
                return false;
            } };
        LEVEL_CHECK(art::Extract("ssa", unreadable, {}, art::LevelNames).levelNames);
        cached.Set("reader", 0);
        {
            std::ofstream output(file);
            output << cached.Dump();
        }
        LEVEL_CHECK(art::Extract("ssa", disc, {}, art::LevelNames).levelNames);
        LEVEL_CHECK(read(file)["reader"].AsInt() == art::LevelNamesVersion);
        LEVEL_CHECK(!art::Extract("unknown", disc, {}, art::LevelNames).levelNames);
        if (old.empty())
            TestUnsetEnv("SKYLANDERS_DATA");
        else
            TestSetEnv("SKYLANDERS_DATA", old.c_str());
        std::filesystem::remove_all(root);
    }
}

int LevelNameChecks(const std::filesystem::path& root)
{
    g_failures = 0;
    DeflateArchiveChecks();
    LuxChecks();
    ScriptChecks();
    IgzChecks();
    MuaChecks();
    CacheChecks(root / "level-names");
    printf("Level name fixtures: %d failures\n", g_failures);
    return g_failures;
}

int LevelNameDiscChecks()
{
    g_failures = 0;
    struct Source
    {
        const char* game;
        const char* variable;
    };
    const Source sources[] = { { "ssa", "SSA_ISO" }, { "giants", "GIANTS_ISO" }, { "swapforce", "SWAPFORCE_ISO" },
        { "trapteam", "TRAPTEAM_ISO" }, { "superchargers", "SUPERCHARGERS_ISO" },
        { "imaginators", "IMAGINATORS_ISO" } };
    for (const auto& source : sources)
    {
        const char* iso = getenv(source.variable);
        if (iso == nullptr || *iso == 0)
        {
            printf("Level names %s: skipped (%s unset)\n", source.game, source.variable);
            continue;
        }
        DiscFileSystem disc;
        if (!disc.Open(iso))
        {
            LEVEL_CHECK(false);
            continue;
        }
        // Never serialize disc text or print it: the real-disc check retains all extracted strings in memory.
        Json levels = art::ReadLevelNames(std::string_view(source.game), art::ReadFrom(disc));
        LEVEL_CHECK(!levels.Members().empty());
        std::map<std::string, int> languages, kinds;
        for (const auto& [id, texts] : levels.Members())
        {
            LEVEL_CHECK(!id.empty() && id.find('\\') == std::string::npos);
            const std::string game = source.game;
            if (game == "ssa")
                LEVEL_CHECK(disc.Find("levels/" + id) != nullptr);
            else if (game == "giants" || game == "trapteam")
                LEVEL_CHECK(disc.Find("level/" + id + ".bld") != nullptr);
            else
                LEVEL_CHECK(disc.Find("archives/" + id.substr(id.find_last_of('/') + 1) + ".pak") != nullptr);
            std::string kind = id.find('/') != std::string::npos ? "zone" : "story";
            if (id.starts_with("challenge_level_"))
                kind = "challenge";
            else if (id.starts_with("pvp_level_"))
                kind = "pvp";
            else if (id.starts_with("level_hub"))
                kind = "hub";
            else if (id.starts_with("horde_level_"))
                kind = "horde";
            kinds[kind]++;
            for (const auto& [language, text] : texts.Members())
            {
                LEVEL_CHECK(text.IsString() && !text.AsString().empty());
                languages[language]++;
            }
        }
        printf("Level names %s: %zu identifiers;", source.game, levels.Members().size());
        for (const auto& [language, count] : languages)
            printf(" %s=%d", language.c_str(), count);
        printf("\n");
        printf("  kinds:");
        for (const auto& [kind, count] : kinds)
            printf(" %s=%d", kind.c_str(), count);
        printf("\n");
    }
    return g_failures;
}
