#include "launcher/online/session_launch.h"
#include "launcher/atomic_file.h"
#include "launcher/ui/collection_model.h"
#include "shared/collection/collection.h"
#include "shared/collection/datadir.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace online
{
    namespace fs = std::filesystem;

    namespace
    {
        // session.txt in the session folder: what FinishSession needs once the game is gone.
        constexpr char Manifest[] = "session.txt";

        std::string View(std::span<const uint8_t> bytes)
        {
            return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
        }

        bool WriteBytes(const fs::path& path, std::span<const uint8_t> bytes)
        {
            return launcher_files::WriteAtomic(path, View(bytes));
        }

        bool ReadBytes(const fs::path& path, std::string& bytes)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            std::ostringstream all;
            all << in.rdbuf();
            bytes = all.str();
            return true;
        }

        void Set(GameLaunch& launch, const std::string& name, const std::string& value)
        {
            std::erase_if(launch.variables, [&](const auto& entry) { return entry.first == name; });
            launch.variables.emplace_back(name, value);
        }

        std::string FigureName(bool host, size_t index, bool bottom)
        {
            return std::string(host ? "p1-" : "p2-") + std::to_string(index) + (bottom ? "-bottom" : "") + ".sky";
        }

        std::string Hex(std::span<const uint8_t> bytes)
        {
            static constexpr char Digits[] = "0123456789abcdef";
            std::string out;
            for (const uint8_t b : bytes)
            {
                out += Digits[b >> 4];
                out += Digits[b & 15];
            }
            return out;
        }
    }

    std::vector<std::string> SessionSeats(
        const collection::Game& game, const Player& hostPlayer, const Player& guestPlayer)
    {
        std::vector<std::string> seats(game.seats.size());
        std::vector<size_t> player1, player2;
        for (size_t i = 0; i < game.seats.size(); ++i)
        {
            if (game.seats[i].player == 1)
                player1.push_back(i);
            else if (game.seats[i].player == 2)
                player2.push_back(i);
        }
        for (size_t i = 0; i < hostPlayer.figures.size(); ++i)
        {
            const size_t seat = hostPlayer.figures[i].seat;
            // The host's own player-2 seats stay empty: Player 2 is the guest.
            if (seat < seats.size() && game.seats[seat].player != 2 && seats[seat].empty())
                seats[seat] = FigureName(true, i, false);
        }
        for (size_t i = 0; i < guestPlayer.figures.size(); ++i)
        {
            const size_t seat = guestPlayer.figures[i].seat;
            const auto rank = std::find(player1.begin(), player1.end(), seat);
            if (rank == player1.end())
                continue;
            const size_t n = size_t(rank - player1.begin());
            if (n < player2.size() && seats[player2[n]].empty())
                seats[player2[n]] = FigureName(false, i, false);
        }
        return seats;
    }

    bool PrepareSession(const collection::Game& game, const SessionPlan& plan, const fs::path& root, GameLaunch& launch,
        std::string& error)
    {
        std::error_code ec;
        fs::remove_all(root, ec);
        const fs::path saves = root / "saves", figures = root / "figures", pictures = root / "pictures";
        for (const fs::path& folder : { saves, figures, pictures })
            if (fs::create_directories(folder, ec); ec)
            {
                error = "cannot create " + datadir::Text(folder);
                return false;
            }
        std::ostringstream manifest;
        manifest << "host=" << (plan.host ? 1 : 0) << "\n";
        for (const SaveFile& file : plan.save)
        {
            if (!SafeSavePath(file.path) || !WriteBytes(saves / datadir::FromText(file.path), file.bytes))
            {
                error = "cannot write the save file " + file.path;
                return false;
            }
            manifest << "save=" << file.path << "\n";
        }
        for (const bool host : { true, false })
        {
            const Player& player = host ? plan.hostPlayer : plan.guestPlayer;
            for (size_t i = 0; i < player.figures.size(); ++i)
            {
                const auto& dump = player.figures[i].dump;
                const size_t halves = dump.size() / FigureDumpBytes;
                for (size_t half = 0; half < halves; ++half)
                {
                    const std::string name = FigureName(host, i, half == 1);
                    if (!WriteBytes(figures / name, std::span(dump).subspan(half * FigureDumpBytes, FigureDumpBytes)))
                    {
                        error = "cannot write a figure";
                        return false;
                    }
                    if (host == plan.host && i < plan.localFigureFiles.size())
                    {
                        // A swapper's bottom goes back to the collection file its top names as its partner.
                        collection::Figure top;
                        if (half == 0)
                            manifest << "figure=" << name << "|" << plan.localFigureFiles[i] << "\n";
                        else if (collection::Read(plan.localFigureFiles[i], top) && !top.partner.empty())
                            manifest << "figure=" << name << "|" << collection::PathOf(top.partner) << "\n";
                    }
                }
            }
            const fs::path picture = pictures / (host ? "p1.png" : "p2.png");
            if (!player.picture.empty() && !WriteBytes(picture, player.picture))
            {
                error = "cannot write a picture";
                return false;
            }
        }
        std::vector<std::string> seats = SessionSeats(game, plan.hostPlayer, plan.guestPlayer);
        // A swapper's bottom half, in the slot after its top on a two-slot seat: the session's dumps carry no
        // collection link for WriteSeats to follow.
        std::string bottoms;
        for (size_t seat = 0; seat < seats.size(); ++seat)
        {
            if (seats[seat].empty())
                continue;
            const int slot = launcher_ui::SeatFirstSlot(game, seat);
            const fs::path bottom = figures / (seats[seat].substr(0, seats[seat].size() - 4) + "-bottom.sky");
            if (launcher_ui::SlotSeat(game, slot + 1) == int(seat) && fs::exists(bottom, ec))
                bottoms += std::to_string(slot + 1) + " " + datadir::Text(bottom) + "\n";
            seats[seat] = datadir::Text(figures / seats[seat]);
        }
        std::string seatsText;
        if (!launcher_ui::WriteSeats(game, datadir::Text(saves / "portal.txt"), seats) ||
            (!bottoms.empty() && (!ReadBytes(saves / "portal.txt", seatsText) ||
                                     !launcher_files::WriteAtomic(saves / "portal.txt", seatsText + bottoms))))
        {
            error = "cannot write the portal's seats";
            return false;
        }
        if (!launcher_files::WriteAtomic(root / Manifest, manifest.str()))
        {
            error = "cannot write the session file";
            return false;
        }

        Set(launch, "SKY_SAVE_DIR", datadir::Text(saves));
        Set(launch, "SKY_PORTAL", "emulated");
        std::erase_if(launch.variables, [](const auto& entry) { return entry.first == "SKY_PORTAL_PLACE"; });
        auto player = [&](const std::string& prefix, const Player& who, const char* picture) {
            Set(launch, prefix + "_NAME", who.pseudo);
            Set(launch, prefix + "_XUID", who.xuid);
            Set(launch, prefix + "_PICTURE", who.picture.empty() ? std::string() : datadir::Text(pictures / picture));
        };
        player("SKY_PLAYER", plan.hostPlayer, "p1.png");
        player("SKY_PLAYER2", plan.guestPlayer, "p2.png");
        // The same game on both sides (docs/online.md): what the host chose, whatever the guest's own settings say.
        Set(launch, "SKY_NETPLAY_FPS", std::to_string(plan.settings.fpsLimit));
        if (!plan.settings.language.empty())
            Set(launch, "SKY_LANGUAGE", plan.settings.language);
        else
            std::erase_if(launch.variables, [](const auto& entry) { return entry.first == "SKY_LANGUAGE"; });
        if (!plan.settings.voices.empty())
            Set(launch, "SKY_VOICE_LANGUAGE", plan.settings.voices);
        else
            std::erase_if(launch.variables, [](const auto& entry) { return entry.first == "SKY_VOICE_LANGUAGE"; });
        Set(launch, "SKY_NIGHTMARE_FROM_START", plan.settings.nightmareFromStart ? "1" : "0");
        Set(launch, "SKY_NETPLAY_DETERMINISTIC", "1");
        Set(launch, "SKY_NETPLAY_DELIVERY", "frame");
        Set(launch, "SKY_NETPLAY_SCHEDULE", "cooperative");
        for (const char* key : { "SKY_NETPLAY_HOST", "SKY_NETPLAY_JOIN", "SKY_NETPLAY_LOCAL_PORT", "SKY_NETPLAY_PEER" })
            std::erase_if(launch.variables, [&](const auto& entry) { return entry.first == key; });
        if (plan.host)
        {
            Set(launch, "SKY_NETPLAY_HOST", std::to_string(plan.start.port));
            if (!plan.peerEndpoint.empty())
                Set(launch, "SKY_NETPLAY_PEER", plan.peerEndpoint);
        }
        else
        {
            if (plan.localPort)
                Set(launch, "SKY_NETPLAY_LOCAL_PORT", std::to_string(plan.localPort));
            const bool v6 = plan.hostAddress.find(':') != std::string::npos;
            Set(launch, "SKY_NETPLAY_JOIN",
                (v6 ? "[" + plan.hostAddress + "]" : plan.hostAddress) + ":" + std::to_string(plan.start.port));
        }
        Set(launch, "SKY_NETPLAY_PASSWORD", Hex(plan.start.sessionKey));
        Set(launch, "SKY_NETPLAY_DELAY", std::to_string(plan.start.inputDelay));
        return true;
    }

    bool FinishSession(const fs::path& root, const fs::path& realSaves, std::string& error)
    {
        std::ifstream manifest(root / Manifest);
        if (!manifest)
            return true;
        bool host = false, ok = true;
        std::string line;
        std::vector<std::string> saves;
        std::vector<std::pair<std::string, std::string>> figures;
        while (std::getline(manifest, line))
        {
            if (line == "host=1")
                host = true;
            else if (line.starts_with("save="))
                saves.push_back(line.substr(5));
            else if (line.starts_with("figure="))
            {
                const size_t bar = line.find('|');
                if (bar != std::string::npos)
                    figures.emplace_back(line.substr(7, bar - 7), line.substr(bar + 1));
            }
        }
        manifest.close();
        if (host)
        {
            const fs::path backup = fs::path(realSaves.native() + fs::path("-backup").native());
            // Every file the game left, new ones too (a first save in an empty slot), except the session's own.
            saves.clear();
            std::error_code walk;
            for (fs::recursive_directory_iterator it(root / "saves", walk), end; !walk && it != end; it.increment(walk))
                if (it->is_regular_file(walk))
                {
                    std::string file = datadir::Text(it->path().lexically_relative(root / "saves"));
                    std::replace(file.begin(), file.end(), '\\', '/');
                    if (file != "portal.txt" && file != "online-ended.txt" && file != "online-replay.txt" &&
                        file != "open-online.txt")
                        saves.push_back(std::move(file));
                }
            for (const std::string& file : saves)
            {
                if (!SafeSavePath(file))
                    continue;
                const fs::path from = root / "saves" / datadir::FromText(file),
                               to = realSaves / datadir::FromText(file);
                std::string bytes, old;
                if (!ReadBytes(from, bytes))
                    continue;
                if (ReadBytes(to, old))
                {
                    if (old == bytes)
                        continue;
                    if (!launcher_files::WriteAtomic(backup / datadir::FromText(file), old))
                    {
                        error = "cannot back up the save file " + file;
                        ok = false;
                        continue;
                    }
                }
                if (!launcher_files::WriteAtomic(to, bytes))
                {
                    error = "cannot write back the save file " + file;
                    ok = false;
                }
            }
        }
        for (const auto& [session, own] : figures)
        {
            std::string bytes, old;
            if (session.find('/') != std::string::npos || session.find('\\') != std::string::npos ||
                !ReadBytes(root / "figures" / session, bytes) || bytes.size() != FigureDumpBytes)
                continue;
            if (ReadBytes(datadir::FromText(own), old) && old == bytes)
                continue;
            if (!launcher_files::WriteAtomic(datadir::FromText(own), bytes))
            {
                error = "cannot write back a figure";
                ok = false;
            }
        }
        if (ok)
        {
            std::error_code ec;
            fs::remove(root / Manifest, ec);
        }
        return ok;
    }

    std::string TakeOpenOnline(const fs::path& saves)
    {
        const fs::path file = saves / "open-online.txt";
        std::ifstream in(file);
        if (!in)
            return {};
        std::string game;
        std::getline(in, game);
        in.close();
        std::error_code error;
        fs::remove(file, error);
        if (!game.empty() && game.back() == '\r')
            game.pop_back();
        // Only a plain game id: the file is written by the game, but it sits in a folder the player can reach.
        if (game.empty() || game.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
            return {};
        return game;
    }
}
