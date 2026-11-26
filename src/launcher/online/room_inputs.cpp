#include "launcher/online/room_inputs.h"
#include <vector>
#include <cstdio>
#include "launcher/game_install.h"
#include "launcher/updater.h"
#include "launcher/game_run.h"
#include "launcher/game_settings.h"
#include "launcher/i18n.h"
#include "launcher/ui/collection_model.h"
#include "shared/collection/datadir.h"
#include <algorithm>
#include <fstream>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace online
{
    namespace room_inputs
    {
        std::string Get(
            const LauncherConfig& config, std::string_view section, const char* key, std::string fallback = {})
        {
            auto s = config.find(std::string(section));
            if (s == config.end())
                return fallback;
            auto v = s->second.find(key);
            return v == s->second.end() ? fallback : v->second;
        }

        bool Bool(std::string value)
        {
            std::transform(
                value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return value == "1" || value == "true" || value == "yes" || value == "on";
        }

        bool Read(const std::filesystem::path& file, std::vector<uint8_t>& bytes, uint64_t limit)
        {
            std::error_code ec;
            const auto size = std::filesystem::file_size(file, ec);
            if (ec || size > limit)
                return false;
            std::ifstream input(file, std::ios::binary);
            bytes.resize(size_t(size));
            return bool(input) &&
                   (bytes.empty() || bool(input.read(reinterpret_cast<char*>(bytes.data()), bytes.size())));
        }

        bool Backup(const std::filesystem::path& relative)
        {
            for (const auto& part : relative)
            {
                const std::string name = datadir::Text(part);
                if (name == "portal.txt" || name == "online-replay.txt" || name == "backups" || name == "backup" ||
                    name == ".backups" || name.ends_with("-backup") || name.ends_with(".bak") ||
                    name.ends_with(".backup"))
                    return true;
            }
            return false;
        }
    }

    std::string RoomPlatform()
    {
#if defined(__APPLE__)
        std::string os = "macos";
#elif defined(_WIN32)
        std::string os = "windows";
#else
        std::string os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
        return os + "-arm64";
#elif defined(__x86_64__) || defined(_M_X64)
        return os + "-x64";
#else
        return os + "-x86";
#endif
    }

    Settings RoomSettings(const LauncherConfig& config, std::string_view game)
    {
        using room_inputs::Get;
        Settings settings;
        const std::string fps = Get(config, GameVideoSection(game), "fps_limit", DefaultFrameRate(game));
        const bool sixty =
            fps == "60" || fps == "display" || fps == "0" || fps == "120" || fps == "144" || fps == "240";
        settings.fpsLimit = fps == "30" ? 30 : sixty ? 60 : uint8_t(std::stoi(DefaultFrameRate(game)));
        const art::DiscLanguages languages = StoredDiscLanguages(config, game);
        settings.language = GameLanguageToPass(Get(config, game, "language"), languages.text, true, i18n::Current());
        settings.voices =
            GameLanguageToPass(Get(config, game, "voice_language"), languages.voice, false, i18n::Current());
        const std::string nightmare = Get(config, game, "nightmare_from_start", "0");
        settings.nightmareFromStart = room_inputs::Bool(nightmare);
        return settings;
    }

    GameOnline ReadGameOnline(const LauncherConfig& config, std::string_view game)
    {
        using namespace room_inputs;
        const std::string section = std::string(game) + ".online";
        GameOnline online;
        const std::string mode = Get(config, section, "mode");
        online.mode = mode == "host" ? Mode::Host : mode == "join" ? Mode::Join : Mode::Solo;
        const std::string port = Get(config, section, "port");
        char* end = nullptr;
        const unsigned long value = port.empty() ? 0 : std::strtoul(port.c_str(), &end, 10);
        if (value >= 1 && value <= 65535 && end != nullptr && *end == 0)
            online.port = uint16_t(value);
        online.automatic = Bool(Get(config, section, "open_port", "1"));
        online.address = Get(config, section, "address");
        online.crossPlatform = Bool(Get(config, section, "cross_platform", "0"));
        return online;
    }

    void WriteGameOnline(LauncherConfig& config, std::string_view game, const GameOnline& online)
    {
        auto& section = config[std::string(game) + ".online"];
        section["mode"] = online.mode == Mode::Host ? "host" : online.mode == Mode::Join ? "join" : "solo";
        section["port"] = std::to_string(online.port);
        section["open_port"] = online.automatic ? "1" : "0";
        section["address"] = online.address;
        section.erase("password");
        section["cross_platform"] = online.crossPlatform ? "1" : "0";
    }

    RoomInputs ReadRoomInputs(const LauncherConfig& config, const profiles::Profile& profile,
        const collection::Game& game, const std::string& xexSha256, bool host, const std::string& seatsFile,
        const std::filesystem::path& saves, bool readSave)
    {
        using namespace room_inputs;
        namespace fs = std::filesystem;
        RoomInputs inputs;
        inputs.hello.launcherVersion = LauncherVersion();
        inputs.hello.gameId = game.id;
        inputs.hello.xexSha256 = xexSha256;
        inputs.hello.gameVersion = FindInstalledGame(config, xexSha256).gameVersion;
        inputs.hello.platform = RoomPlatform();
        inputs.hello.crossPlatform = ReadGameOnline(config, game.id).crossPlatform;
        // This PC's pace in this game, from the newest game log when it is this game's.
        if (const GameLog log = LastGameLog(); log.game == game.id && !log.file.empty())
        {
            const PerfHint hint = ReadPerfHint(ReadLogTail(log.file, 64 * 1024));
            inputs.player.fps = hint.fps;
            inputs.player.gpuBound = hint.gpuBound;
        }
        inputs.player.pseudo = profile.name.empty() ? Tr("Player 1") : profile.name;
        inputs.player.xuid = profile.xuid;
        inputs.player.color = uint8_t(std::clamp(profile.color, 0, 9));
        if (profile.hasPicture && !Read(profiles::PictureOf(profile), inputs.player.picture, MaxPictureBytes))
            inputs.error = InputError::Picture;
        const auto seats = launcher_ui::ReadSeats(game, seatsFile);
        auto add = [&](const std::string& path, size_t seat) {
            collection::Figure figure;
            Figure entry;
            if (!collection::Read(path, figure) || !Read(datadir::FromText(path), entry.dump, FigureDumpBytes) ||
                entry.dump.size() != FigureDumpBytes || inputs.player.figures.size() >= MaxFigures)
            {
                inputs.error = InputError::Figure;
                return;
            }
            entry.toyType = figure.type;
            entry.variant = figure.variant;
            entry.name = figure.DisplayName();
            entry.seat = uint8_t(seat);
            inputs.player.figures.push_back(std::move(entry));
            inputs.localFigureFiles.push_back(path);
        };
        for (size_t i = 0; i < seats.size(); ++i)
        {
            if (seats[i].empty() || game.seats[i].player == 2 || (!host && game.seats[i].player == 0))
                continue;
            add(seats[i], i);
            collection::Figure top;
            if (collection::Read(seats[i], top) && top.IsSwapTop() && !top.partner.empty())
                add(launcher_ui::BesideFigure(top.path, top.partner), i);
        }
        inputs.settings = RoomSettings(config, game.id);
        if (!host || !readSave)
            return inputs;
        std::error_code ec;
        if (!fs::exists(saves, ec))
        {
            if (ec)
                inputs.error = InputError::Save;
            return inputs;
        }
        uint64_t total = 0;
        constexpr uint64_t MaxSave = 64 * 1024 * 1024;
        fs::recursive_directory_iterator it(saves, ec), end;
        while (!ec && it != end)
        {
            const fs::path relative = it->path().lexically_relative(saves);
            if (Backup(relative))
            {
                if (it->is_directory(ec))
                    it.disable_recursion_pending();
            }
            else if (it->is_symlink(ec))
            {
                inputs.error = InputError::Save;
                return inputs;
            }
            else if (it->is_regular_file(ec))
            {
                const uint64_t size = it->file_size(ec);
                if (ec)
                    break;
                if (size > MaxSave - total || inputs.save.size() >= std::numeric_limits<uint16_t>::max())
                {
                    inputs.error = InputError::SaveTooLarge;
                    return inputs;
                }
                SaveFile file;
                file.path = datadir::Text(relative);
                std::replace(file.path.begin(), file.path.end(), '\\', '/');
                if (!SafeSavePath(file.path) || !Read(it->path(), file.bytes, MaxSave - total))
                {
                    inputs.error = InputError::Save;
                    return inputs;
                }
                total += file.bytes.size();
                inputs.save.push_back(std::move(file));
            }
            it.increment(ec);
        }
        if (ec)
            inputs.error = InputError::Save;
        std::sort(inputs.save.begin(), inputs.save.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        inputs.settings.saveBytes = uint32_t(total);
        inputs.settings.saveFiles = uint16_t(inputs.save.size());
        return inputs;
    }

    PerfHint ReadPerfHint(std::string_view log)
    {
        std::vector<double> fps, frame, gpu;
        size_t at = 0;
        while ((at = log.find("[perf] ", at)) != std::string_view::npos)
        {
            const size_t end = log.find('\n', at);
            const std::string line(log.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at));
            at = end == std::string_view::npos ? log.size() : end;
            double f = 0, median = 0, wait = 0;
            const size_t medianAt = line.find("frame median "), waitAt = line.find("GPU wait ");
            if (std::sscanf(line.c_str(), "[perf] %lf fps;", &f) != 1 || medianAt == std::string::npos ||
                waitAt == std::string::npos || std::sscanf(line.c_str() + medianAt, "frame median %lf", &median) != 1 ||
                std::sscanf(line.c_str() + waitAt, "GPU wait %lf", &wait) != 1)
                continue;
            fps.push_back(f);
            frame.push_back(median);
            gpu.push_back(wait);
        }
        PerfHint hint;
        if (fps.empty())
            return hint;
        const auto medianOfLast = [](std::vector<double> values) {
            if (values.size() > 20)
                values.erase(values.begin(), values.end() - 20);
            std::sort(values.begin(), values.end());
            return values[values.size() / 2];
        };
        hint.fps = uint8_t(std::clamp(medianOfLast(fps) + 0.5, 0.0, 240.0));
        hint.gpuBound = medianOfLast(gpu) * 3 >= medianOfLast(frame);
        return hint;
    }

    int SessionScale(const PerfHint& hint, int playerScale)
    {
        return hint.fps != 0 && hint.fps < 28 && hint.gpuBound && playerScale > 1 ? playerScale - 1 : 0;
    }
}
