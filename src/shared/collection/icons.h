#pragma once

// A picture for any toy: the one the player's own disc has (cached as PNG by the game, docs/runtime/host/figures.md), else one drawn
// here: a shape per category in the element's colour. Variants get a badge (Legendary star, Dark crescent...).
// Nothing is shipped or downloaded: the shapes are code.
#include "catalogue.h"
#include <cstdint>
#include <string>
#include <vector>

namespace collection
{
    struct Image
    {
        int width = 0, height = 0;
        std::vector<uint32_t> rgba; // R in the low byte, straight (not premultiplied) alpha

        bool Empty() const { return rgba.empty(); }
    };

    // The mark Spyro's Adventure's menus give a toy family (magic items: a potion; adventure packs: a compass; minis and
    // sidekicks: a paw): a signed distance (negative inside) in a [-1, 1] square, y down. Arkeyan Launcher draws it over its
    // own icon shapes, so both apps show the same mark. Other families have none (KindGlyph returns 1).
    bool HasKindGlyph(Category category);
    float KindGlyph(Category category, float x, float y);

    // The category's shape on a transparent square (on an opaque rounded tile with `tile`).
    Image GeneratedIcon(const Toy* toy, int size, bool tile = false);
    // A round badge for the variant kind in the bottom right corner; nothing for Base.
    void AddBadge(Image& image, VariantKind kind);

    // A toy's picture from one game's disc, as cached by art::Extract (art/game_art.h): the picture of its variant
    // (`variant`: the variant id, 0 for the plain toy) if the game has one, else the toy's. `large`: the game's biggest
    // (SSA: the 512 px "versus" picture; Giants has one size, 256 px). Empty when that game has none cached.
    // Folders: SKY_PORTRAITS, else "portraits" in the game's data folder.
    std::string GamePortraitPath(const std::string& gameId, const Toy& toy, uint16_t variant = 0, bool large = false);
    // The lookups answer from each folder's listing, read once and again when the folder changes (looked at once a second
    // at most), with one stat for the picture found: any thread may call them. A picture written by SavePng is found at
    // once, one written otherwise within a second, or at once after ForgetPictureFolders; a deleted one is never returned.
    void ForgetPictureFolders();
    size_t PictureFolderListings(); // folders read from the disk so far (for the tests)

    // A villain's picture from the game's disc (Game::villains; Trap Team's vault: "captured" is the villain held in a trap,
    // else the one escaped). Empty when it is not cached.
    std::string VillainPicturePath(const std::string& gameId, const Villain& villain, bool captured = true);

    // A toy family's picture from one game's disc (Game::kindPictures: SSA's magic item, location and sidekick icons),
    // empty when that game has none cached.
    std::string GameKindPicturePath(const std::string& gameId, Category category);

    // --- Which game's pictures (the player's choice; docs/runtime/host/figures.md, "Which picture") ----------------------

    // Kept in the collection folder's pictures/pictures.json with the player's own pictures (pictures/<type>-<variant>.png),
    // so the choices travel with the collection; the launcher and every game read the same file.
    inline constexpr char PicturesOrigin[] = "origin"; // the figure's original game (the default)
    inline constexpr char PicturesNewest[] = "newest"; // the newest game that has one
    inline constexpr char PictureOwn[] = "own"; // a card's own picture
    // The choice for every figure: PicturesOrigin (also when unset or unknown), PicturesNewest or a game id.
    std::string PicturesDefault();
    bool SetPicturesDefault(const std::string& choice);
    // One card's choice (a toy type and variant id; a swapper half on its own): empty for the default, a game id, or
    // PictureOwn while its own picture is there.
    std::string PictureChoice(uint16_t type, uint16_t variant);
    // Empty clears it; PictureOwn only through SetOwnPicture. False when the file cannot be written.
    bool SetPictureChoice(uint16_t type, uint16_t variant, const std::string& choice);
    // The player's own picture for a card, kept square (transparent margins) and at most OwnPictureSize pixels wide;
    // the card's choice becomes PictureOwn. RemoveOwnPicture deletes it and goes back to the default.
    inline constexpr int OwnPictureSize = 512;
    bool SetOwnPicture(uint16_t type, uint16_t variant, const Image& image, std::string* error = nullptr);
    bool RemoveOwnPicture(uint16_t type, uint16_t variant);
    // The card's own picture file, empty when it has none.
    std::string OwnPicturePath(uint16_t type, uint16_t variant);
    // The games (catalogue order) with a cached picture of that card, for a list to choose from.
    std::vector<std::string> PictureGames(const Toy& toy, uint16_t variant);
    // Changes whenever a choice or an own picture changes, in this process at once, in another one within a second:
    // a cache of decoded pictures is dropped when it changes. Cheap enough for every frame.
    uint64_t PictureGeneration();

    // The shared picture rule. The games in order: the card's chosen game (its variant art, then its plain art), then
    // the default choice's game, then the variant's original game (Catalogue::Origin), the games after it (nearest
    // first) and the ones before it (nearest first); PicturesNewest puts the newest first instead. Within that order a
    // variant's own art comes before plain art, then a family picture, then empty for a generated icon. A card's own
    // picture comes before all of it. gameId is kept for caller compatibility; viewing a different game never changes
    // the chosen art. large selects the larger portrait within that game.
    // Swapper halves first use their own half art, then whole-figure art (a bottom may use its matching top's).
    std::string PortraitPath(const std::string& gameId, const Toy& toy, uint16_t variant = 0, bool large = false);
    // The same rule, preferring artwork to a portrait within each game, never an older game's plain artwork to a
    // newer game's portrait. Empty means the caller draws the generated icon.
    std::string BestPicturePath(const std::string& gameId, const Toy& toy, uint16_t variant = 0);
    // When only the kind is known (the Toy Box, the collection's cards): the toy's one variant of that kind, else 0.
    uint16_t VariantFor(const Toy& toy, VariantKind kind);
    // A swapper half's own picture, else its half art in the same order (variant before plain), then whole-figure
    // art, then empty for a generated icon. A bottom's matching top can supply the whole figure. Empty for toys that are not halves.
    std::string HalfPicturePath(const Toy& toy, uint16_t variant = 0);
    // `image` cut to its visible pixels (alpha above `minAlpha`: its bounding box); unchanged when none is visible.
    Image CropToVisible(const Image& image, uint8_t minAlpha = 8);
    // The two halves of a swapper joined, as the game's swap screen shows them: each half is drawn in place on one shared
    // square frame (the sizes differ between toys: both frames are brought to the larger one's size first), the top
    // composited over the bottom (straight alpha), the result cut to its visible box. No stacking, no rescaling of a half
    // on its own. Empty when either half has no visible pixel.
    Image JoinHalves(const Image& top, const Image& bottom);
    // A swapper's picture: its two genuine half images joined, each in the picture rule's order. Empty when either half
    // has none or its own picture, or dedicated variant art would be lost to plain halves (the caller uses the top's
    // picture with the bottom small in a corner). The same picture in the
    // launcher, the figure sheet, Add a figure, the seats and the Toy Box.
    Image SwapperPicture(const Toy& top, uint16_t topVariant, const Toy& bottom, uint16_t bottomVariant);
    // A creation crystal's picture in its element's colour: the disc has one magenta crystal for the ten elements (the game
    // tints it as it draws), so its magenta glass takes the element's colour here, shade kept. `path`: the picture the
    // caller found (BestPicturePath, PortraitPath). Empty for another toy or without a picture.
    Image CrystalPicture(const Toy& toy, const std::string& path);
    // PortraitPath with no game being viewed, as for every other view.
    std::string DiscImagePath(const Toy& toy);

    // The picture PortraitPath finds, else the generated one, `size` pixels square, with the badge of `kind`.
    Image Portrait(const std::string& gameId, const Toy* toy, VariantKind kind, int size, uint16_t variant = 0,
        bool* fromDisc = nullptr);
    // Portrait with no game being viewed.
    Image Icon(const Toy* toy, VariantKind kind, int size, bool* fromDisc = nullptr);

    Image LoadPng(const std::string& path);
    bool SavePng(const Image& image, const std::string& path);
    Image Resize(const Image& image, int width, int height);
}
