#include "assets/launcher_assets.h"
#include "launcher/updater.h"
#include "shared/art/game_art.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stb_image.h>
#include <vector>

namespace LauncherAssets
{
    namespace
    {
        constexpr const char* DownloadedLogo = "logo-download.png";

        // The manifest's cleaner logo (art::LogoDownload), once: a picture of a sensible size, else nothing is kept. Cut
        // after 10 s of transfer (curl's own limit to connect is 15 s); the next disc check tries again.
        void DownloadLogo(const std::string& game)
        {
            const std::string url = art::LogoDownload(game);
            const std::filesystem::path cache = art::CacheFolder(game);
            std::error_code error;
            if (url.empty() || cache.empty() || std::filesystem::exists(cache / DownloadedLogo, error))
                return;
            const std::filesystem::path file = cache / "logo-download.new";
            std::atomic<bool> stop{ false };
            const uint64_t began = SDL_GetTicks();
            HttpResult result = HttpDownload(url, file, [&](uint64_t) { stop = SDL_GetTicks() - began > 10000; }, &stop);
            int width = 0, height = 0, channels = 0;
            std::ifstream in(file, std::ios::binary);
            const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
            in.close();
            if (result.ok && !bytes.empty() && bytes.size() < 16 * 1024 * 1024 &&
                stbi_info_from_memory(bytes.data(), int(bytes.size()), &width, &height, &channels) && width >= 64 &&
                height >= 32 && width <= 4096 && height <= 4096)
                std::filesystem::rename(file, cache / DownloadedLogo, error);
            else
                fprintf(stderr, "[art] %s: the logo was not downloaded (%s)\n", game.c_str(),
                    result.ok ? "not a picture" : result.error.c_str());
            std::filesystem::remove(file, error);
        }

        art::Role ArtRole(Role role)
        {
            switch (role)
            {
            case Role::Logo: return art::Role::Logo;
            case Role::HeroBackground: return art::Role::Background;
            case Role::TitleIcon: return art::Role::TitleIcon;
            }
            return art::Role::Logo;
        }
    }

    void Provider::Init(const std::string& gameId)
    {
        game = gameId;
        enabled = !art::CacheFolder(gameId).empty();
    }

    void Provider::ExtractFromDisc(DiscFileSystem& disc, const TitleMetadata& meta)
    {
        if (!enabled)
            return;
        art::Extract(game, art::ReadFrom(disc), meta.iconPng);
        DownloadLogo(game); // only now: the player has the game's disc
    }

    Asset Provider::Get(Role role) const
    {
        return enabled ? Cached(game, role) : Asset{};
    }

    Asset Cached(const std::string& gameId, Role role)
    {
        std::error_code error;
        if (const std::filesystem::path cache = art::CacheFolder(gameId); role == Role::Logo && !cache.empty() &&
            std::filesystem::is_regular_file(cache / DownloadedLogo, error))
            return { (cache / DownloadedLogo).string(), true };
        std::string path = art::CachedPath(gameId, ArtRole(role));
        return { path, !path.empty() };
    }
} // namespace LauncherAssets
