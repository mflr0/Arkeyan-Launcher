// Writes the pictures Discord Rich Presence names (src/launcher/discord_presence.h), ready to upload as the application's
// art assets on discord.com/developers (Rich Presence > Art Assets; each file's name is its key):
//   <game id>.png     the game's logo, as the game page shows it, on a transparent square: the large image
//   fig_<type>.png    each character's picture, as the Toy Box shows it: the corner image while it is on the portal
// The pictures come from this computer's own discs (the launcher's caches); a toy no disc has a picture of gets the
// launcher's drawn icon. They are game art: never commit them, upload them to the application only.
// Characters only (the toys that level up, 204 of them): Discord keeps at most 300 assets per application.
//   cmake --build build --target DiscordFigureIcons && build/DiscordFigureIcons <output folder>
#include "shared/art/game_art.h"
#include "shared/collection/catalogue.h"
#include "shared/collection/icons.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <stb_image.h>
#include <stb_image_write.h>

namespace
{
    // The logo the disc check cached for `game` (in launcher-cache/, or a title's folder in it), centred on a transparent
    // square of at least 512 px, Discord's smallest art asset.
    bool WriteLogo(const std::string& game, const std::filesystem::path& out)
    {
        const std::filesystem::path cache = art::CacheFolder(game);
        std::error_code error;
        if (cache.empty() || !std::filesystem::is_directory(cache, error))
            return false;
        std::filesystem::path logo;
        for (auto it = std::filesystem::recursive_directory_iterator(cache, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
            if (it->path().filename() == art::RoleFile(art::Role::Logo))
            {
                logo = it->path();
                break;
            }
        int width = 0, height = 0, channels = 0;
        unsigned char* pixels = logo.empty() ? nullptr : stbi_load(logo.string().c_str(), &width, &height, &channels, 4);
        if (pixels == nullptr)
            return false;
        const int size = std::max({ width, height, 512 });
        std::vector<unsigned char> square(size_t(size) * size * 4, 0);
        const int left = (size - width) / 2, top = (size - height) / 2;
        for (int y = 0; y < height; y++)
            std::copy(pixels + size_t(y) * width * 4, pixels + size_t(y + 1) * width * 4, square.begin() + (size_t(y + top) * size + left) * 4);
        stbi_image_free(pixels);
        return stbi_write_png(out.string().c_str(), size, size, 4, square.data(), size * 4) != 0;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: DiscordFigureIcons <output folder>\n");
        return 2;
    }
    constexpr int Size = 256;
    const std::filesystem::path folder = argv[1];
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const collection::Catalogue& catalogue = collection::Catalogue::Get();

    int logos = 0;
    for (const collection::Game& game : catalogue.Games())
        if (WriteLogo(game.id, folder / (game.id + ".png")))
            logos++;

    int figures = 0, fromDiscs = 0;
    for (const collection::Toy& toy : catalogue.Toys())
    {
        if (toy.category > collection::Category::Mini) // the toys that level up: the ones the corner shows
            continue;
        bool fromDisc = false;
        const collection::Image image = collection::Icon(&toy, toy.edition, Size, &fromDisc);
        if (image.Empty())
            continue;
        const std::string file = (folder / ("fig_" + std::to_string(toy.type) + ".png")).string();
        if (!stbi_write_png(file.c_str(), image.width, image.height, 4, image.rgba.data(), image.width * 4))
        {
            std::fprintf(stderr, "could not write %s\n", file.c_str());
            return 1;
        }
        figures++;
        fromDiscs += fromDisc ? 1 : 0;
    }
    std::printf("%d logos, %d characters (%d from the discs, %d drawn) in %s\n", logos, figures, fromDiscs, figures - fromDiscs,
        folder.string().c_str());
    return 0;
}
