#include "icons.h"
#include "collection.h"
#include "datadir.h"
#include "json.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include "../env.h"

namespace collection
{
    namespace
    {
        // Shapes are signed distances in a [-1, 1] square (negative inside), y down.
        using Shape = std::function<float(float x, float y)>;
        constexpr float Pi = 3.14159265f;

        struct Point
        {
            float x, y;
        };

        float Circle(float x, float y, float cx, float cy, float r) { return std::hypot(x - cx, y - cy) - r; }

        float Box(float x, float y, float cx, float cy, float hw, float hh, float radius)
        {
            float dx = std::fabs(x - cx) - hw + radius, dy = std::fabs(y - cy) - hh + radius;
            return std::hypot(std::max(dx, 0.0f), std::max(dy, 0.0f)) + std::min(std::max(dx, dy), 0.0f) - radius;
        }

        // Any simple polygon: distance to the nearest edge, negative inside (even-odd rule).
        float Polygon(float x, float y, const std::vector<Point>& points)
        {
            float best = 1e9f;
            bool inside = false;
            for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++)
            {
                Point a = points[j], b = points[i];
                float ex = b.x - a.x, ey = b.y - a.y, px = x - a.x, py = y - a.y;
                float t = std::clamp((px * ex + py * ey) / (ex * ex + ey * ey), 0.0f, 1.0f);
                best = std::min(best, std::hypot(px - ex * t, py - ey * t));
                if ((a.y > y) != (b.y > y) && x < a.x + (y - a.y) * ex / ey)
                    inside = !inside;
            }
            return inside ? -best : best;
        }

        std::vector<Point> Star(int points, float outer, float inner, float cx = 0, float cy = 0, float turn = 0)
        {
            std::vector<Point> star;
            for (int i = 0; i < points * 2; i++)
            {
                float angle = Pi * (i / float(points) - 0.5f) + turn, r = i % 2 == 0 ? outer : inner;
                star.push_back({ cx + r * std::cos(angle), cy + r * std::sin(angle) });
            }
            return star;
        }

        std::vector<Point> Regular(int sides, float r, float turn = 0)
        {
            std::vector<Point> polygon;
            for (int i = 0; i < sides; i++)
            {
                float angle = 2 * Pi * i / sides + turn;
                polygon.push_back({ r * std::cos(angle), r * std::sin(angle) });
            }
            return polygon;
        }

        // Skylanders are shields: big for Giants, small for minis.
        Shape Shield(float scale, float dy)
        {
            std::vector<Point> shield = { { -0.62f, -0.72f }, { 0, -0.86f }, { 0.62f, -0.72f }, { 0.62f, 0.05f },
                { 0, 0.86f }, { -0.62f, 0.05f } };
            for (Point& point : shield)
                point = { point.x * scale, point.y * scale + dy };
            return [shield](float x, float y) { return Polygon(x, y, shield) - 0.04f; };
        }

        Shape CategoryShape(Category category)
        {
            if (HasKindGlyph(category))
                return [category](float x, float y) { return KindGlyph(category, x, y); };
            switch (category)
            {
            case Category::Core: return Shield(0.8f, 0);
            case Category::Giant: return Shield(1.0f, 0);
            case Category::SwapTop:
                return [](float x, float y) { return std::max(Circle(x, y, 0, 0.1f, 0.72f), y - 0.1f); };
            case Category::SwapBottom:
                return [](float x, float y) { return std::max(Circle(x, y, 0, -0.1f, 0.72f), -0.1f - y); };
            case Category::TrapMaster:
                return [p = Regular(8, 0.76f, Pi / 8)](float x, float y) { return Polygon(x, y, p); };
            case Category::SuperCharger:
            {
                std::vector<Point> bolt = { { 0.15f, -0.85f }, { -0.5f, 0.1f }, { -0.05f, 0.1f }, { -0.2f, 0.85f },
                    { 0.5f, -0.15f }, { 0.05f, -0.15f } };
                return [bolt](float x, float y) { return Polygon(x, y, bolt); };
            }
            case Category::Sensei: return [p = Star(5, 0.85f, 0.4f)](float x, float y) { return Polygon(x, y, p); };
            case Category::Trap:
            {
                std::vector<Point> crystal = { { 0, -0.85f }, { 0.45f, -0.4f }, { 0.45f, 0.4f }, { 0, 0.85f },
                    { -0.45f, 0.4f }, { -0.45f, -0.4f } };
                return [crystal](float x, float y) { return Polygon(x, y, crystal); };
            }
            case Category::Villain:
            {
                std::vector<Point> horns = { { -0.75f, -0.7f }, { -0.3f, -0.35f }, { 0.3f, -0.35f }, { 0.75f, -0.7f },
                    { 0.55f, 0.2f }, { 0, 0.8f }, { -0.55f, 0.2f } };
                return [horns](float x, float y) { return Polygon(x, y, horns); };
            }
            case Category::Vehicle:
                return [](float x, float y) {
                    float ring = std::max(Circle(x, y, 0, 0, 0.78f), -Circle(x, y, 0, 0, 0.5f));
                    float spokes = std::min(Box(x, y, 0, 0, 0.08f, 0.6f, 0.02f), Box(x, y, 0, 0, 0.6f, 0.08f, 0.02f));
                    return std::min({ ring, spokes, Circle(x, y, 0, 0, 0.2f) });
                };
            case Category::CreationCrystal:
            {
                std::vector<Point> gem = { { 0, -0.88f }, { 0.55f, -0.1f }, { 0, 0.88f }, { -0.55f, -0.1f } };
                return [gem](float x, float y) { return Polygon(x, y, gem); };
            }
            case Category::Trophy:
            {
                std::vector<Point> cup = { { -0.7f, -0.75f }, { 0.7f, -0.75f }, { 0.4f, 0.0f }, { 0.12f, 0.15f },
                    { 0.12f, 0.5f }, { 0.45f, 0.55f }, { 0.45f, 0.8f }, { -0.45f, 0.8f }, { -0.45f, 0.55f },
                    { -0.12f, 0.5f }, { -0.12f, 0.15f }, { -0.4f, 0.0f } };
                return [cup](float x, float y) { return Polygon(x, y, cup); };
            }
            default: return [](float x, float y) { return Circle(x, y, 0, 0, 0.66f); };
            }
        }

        // The badge's mark, in the badge's own [-1, 1] square.
        Shape KindMark(VariantKind kind)
        {
            switch (kind)
            {
            case VariantKind::Legendary:
            case VariantKind::EonsElite:
            case VariantKind::Event:
            case VariantKind::Gold: return [p = Star(5, 0.7f, 0.3f)](float x, float y) { return Polygon(x, y, p); };
            case VariantKind::Dark:
                return [](float x, float y) {
                    return std::max(Circle(x, y, 0, 0, 0.6f), -Circle(x, y, 0.3f, -0.25f, 0.5f));
                };
            case VariantKind::Crystal:
            case VariantKind::Metallic:
            case VariantKind::Silver:
            case VariantKind::Chrome:
            {
                std::vector<Point> gem = { { 0, -0.65f }, { 0.5f, 0 }, { 0, 0.65f }, { -0.5f, 0 } };
                return [gem](float x, float y) { return Polygon(x, y, gem); };
            }
            case VariantKind::Glow:
            case VariantKind::LightCore:
                return [](float x, float y) { return std::max(Circle(x, y, 0, 0, 0.62f), -Circle(x, y, 0, 0, 0.32f)); };
            case VariantKind::Series:
                return [](float x, float y) {
                    return std::min(Circle(x, y, -0.3f, 0, 0.22f), Circle(x, y, 0.3f, 0, 0.22f));
                };
            case VariantKind::Flocked:
                return [](float x, float y) {
                    return std::min({ Circle(x, y, -0.32f, 0.2f, 0.2f), Circle(x, y, 0.32f, 0.2f, 0.2f),
                        Circle(x, y, 0, -0.3f, 0.2f) });
                };
            case VariantKind::Shape: return [p = Regular(6, 0.6f)](float x, float y) { return Polygon(x, y, p); };
            default: return [](float x, float y) { return Circle(x, y, 0, 0, 0.3f); };
            }
        }

        struct Rgba
        {
            float r, g, b, a;
        };

        Rgba FromRgb(uint32_t rgb, float alpha = 1)
        {
            return { ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha };
        }

        Rgba Mix(Rgba a, Rgba b, float t)
        {
            return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t };
        }

        Rgba Unpack(uint32_t p)
        {
            return { (p & 0xFF) / 255.0f, ((p >> 8) & 0xFF) / 255.0f, ((p >> 16) & 0xFF) / 255.0f, (p >> 24) / 255.0f };
        }

        uint32_t Pack(Rgba c)
        {
            auto byte = [](float v) { return uint32_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
            return byte(c.r) | byte(c.g) << 8 | byte(c.b) << 16 | byte(c.a) << 24;
        }

        // `top` over `bottom`, straight alpha.
        Rgba Over(Rgba top, Rgba bottom)
        {
            float a = top.a + bottom.a * (1 - top.a);
            if (a <= 0)
                return { 0, 0, 0, 0 };
            auto channel = [&](float t, float b) { return (t * top.a + b * bottom.a * (1 - top.a)) / a; };
            return { channel(top.r, bottom.r), channel(top.g, bottom.g), channel(top.b, bottom.b), a };
        }

        // Coverage of a pixel by a shape whose distance is `d` (in pixels).
        float Coverage(float d) { return std::clamp(0.5f - d, 0.0f, 1.0f); }

        // Paints `shape` (unit square mapped onto the square at x0, y0 of `extent` pixels): fill, then an outline.
        void Paint(Image& image, float x0, float y0, float extent, const Shape& shape, Rgba fill, Rgba light,
            Rgba outline, float outlineWidth)
        {
            float scale = extent / 2; // pixels per unit
            int left = std::max(int(x0), 0), top = std::max(int(y0), 0);
            int right = std::min(int(std::ceil(x0 + extent)), image.width),
                bottom = std::min(int(std::ceil(y0 + extent)), image.height);
            for (int py = top; py < bottom; py++)
            {
                for (int px = left; px < right; px++)
                {
                    float ux = (px + 0.5f - x0) / scale - 1, uy = (py + 0.5f - y0) / scale - 1;
                    float d = shape(ux, uy) * scale;
                    if (d > 1)
                        continue;
                    // A soft light from the top left, then the outline band.
                    Rgba color = Mix(light, fill, std::clamp((ux + uy + 1) / 2.5f, 0.0f, 1.0f));
                    float edge = Coverage(d + outlineWidth);
                    color = Mix(outline, color, edge);
                    color.a *= Coverage(d);
                    uint32_t& pixel = image.rgba[size_t(py) * image.width + px];
                    pixel = Pack(Over(color, Unpack(pixel)));
                }
            }
        }

        // Colour of toys without an element: the Toy Box's item and adventure colours.
        uint32_t ToyRgb(const Toy* toy)
        {
            if (toy == nullptr)
                return 0x6E7180;
            if (toy->element != Element::None)
                return ElementRgb(toy->element);
            switch (toy->category)
            {
            case Category::MagicItem: return 0x1E8F82;
            case Category::Adventure: return 0xA8406F;
            case Category::Trophy: return 0xC79A2A;
            default: return ElementRgb(Element::None);
            }
        }

        std::string Lower(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(tolower(c)); });
            return text;
        }
    }

    // A few families get the mark Spyro's Adventure's menus use for them (a potion for magic items, a compass for
    // locations, a paw for sidekicks), so a drawn icon reads like the disc's.
    bool HasKindGlyph(Category category)
    {
        return category == Category::MagicItem || category == Category::Adventure || category == Category::Mini;
    }

    float KindGlyph(Category category, float x, float y)
    {
        switch (category)
        {
        case Category::MagicItem:
        {
            // A round flask with a neck and a lip, and a spark at its shoulder.
            float flask = Circle(x, y, 0, 0.28f, 0.52f);
            float neck = Box(x, y, 0, -0.34f, 0.16f, 0.24f, 0.04f);
            float lip = Box(x, y, 0, -0.62f, 0.27f, 0.09f, 0.06f);
            static const std::vector<Point> spark = Star(4, 0.26f, 0.07f, 0.6f, -0.52f);
            return std::min({ flask, neck, lip, Polygon(x, y, spark) });
        }
        case Category::Adventure:
        {
            // A compass: a ring and a four-pointed rose.
            static const std::vector<Point> rose = Star(4, 0.6f, 0.17f);
            float ring = std::max(Circle(x, y, 0, 0, 0.86f), -Circle(x, y, 0, 0, 0.7f));
            return std::min(ring, Polygon(x, y, rose));
        }
        case Category::Mini:
        {
            // A paw: the pad and four toes.
            float pad = std::min(Circle(x, y, 0, 0.36f, 0.36f), Box(x, y, 0, 0.44f, 0.46f, 0.26f, 0.24f));
            return std::min({ pad, Circle(x, y, -0.62f, -0.12f, 0.18f), Circle(x, y, -0.25f, -0.5f, 0.2f),
                Circle(x, y, 0.25f, -0.5f, 0.2f), Circle(x, y, 0.62f, -0.12f, 0.18f) });
        }
        default: return 1;
        }
    }

    Image GeneratedIcon(const Toy* toy, int size, bool tile)
    {
        Image image{ size, size, std::vector<uint32_t>(size_t(size) * size) };
        Rgba base = FromRgb(ToyRgb(toy));
        Rgba white = { 1, 1, 1, 1 }, black = { 0, 0, 0, 1 };
        if (tile)
        {
            Rgba dark = Mix(base, black, 0.55f);
            Paint(
                image, 0, 0, float(size), [](float x, float y) { return Box(x, y, 0, 0, 1, 1, 0.22f); }, dark,
                Mix(dark, base, 0.4f), dark, 0);
        }
        // A pale fill shows on the element-coloured disc the menus draw under it; the outline is the element darkened.
        Category category = toy != nullptr ? toy->category : Category::Core;
        float margin = size * 0.1f;
        Paint(image, margin, margin, size - 2 * margin, CategoryShape(category), Mix(base, white, 0.55f),
            Mix(base, white, 0.85f), Mix(base, black, 0.45f), std::max(1.5f, size / 40.0f));
        return image;
    }

    void AddBadge(Image& image, VariantKind kind)
    {
        if (kind == VariantKind::Base || image.Empty())
            return;
        float extent = image.width * 0.42f, x0 = image.width - extent - image.width * 0.02f,
              y0 = image.height - extent - image.height * 0.02f;
        Rgba color = FromRgb(VariantKindRgb(kind)), black = { 0, 0, 0, 1 }, white = { 1, 1, 1, 1 };
        float outline = std::max(1.5f, image.width / 48.0f);
        Paint(
            image, x0, y0, extent, [](float x, float y) { return Circle(x, y, 0, 0, 0.92f); }, color,
            Mix(color, white, 0.35f), Mix(color, black, 0.6f), outline);
        // The mark: white on dark badges, dark on pale ones.
        float luma = 0.3f * color.r + 0.59f * color.g + 0.11f * color.b;
        Rgba ink = luma > 0.6f ? Mix(color, black, 0.7f) : white;
        Paint(image, x0, y0, extent, KindMark(kind), ink, ink, ink, 0);
    }

    namespace
    {
        // What one folder of cached pictures holds, read once: the callers ask for a picture of every visible card on
        // every frame, and a stat per candidate name was hundreds of filesystem calls per frame.
        struct PictureFolder
        {
            std::map<std::string, std::string> files; // the lower-case name without ".png" -> the file's own name
            SDL_Time modified = 0; // the folder's time when it was listed
            uint64_t checkedAt = 0; // SDL_GetTicks of the last look at that time
            bool settled = false; // listed 3 s after its last change: a coarse clock (FAT: 2 s) shows the next one
        };

        // Each folder is looked at again at most this often, for the pictures another process writes (the game or the
        // launcher reading the disc while the other one runs).
        constexpr uint64_t PictureFolderCheckMs = 1000;
        constexpr SDL_Time PictureFolderSettleNs = SDL_NS_PER_SECOND * 3;

        std::mutex g_pictureFoldersLock; // the launcher looks pictures up from a loading thread too
        std::map<std::string, PictureFolder> g_pictureFolders; // by the folder's path, with its trailing separator
        std::map<std::string, std::string>
            g_portraitFolders; // datadir::Game(id) + "/portraits/", by game id and data root
        size_t g_pictureFolderListings = 0;

        // `path` without its trailing separator, for the filesystem calls.
        std::string Bare(const std::string& path)
        {
            return path.size() > 1 && (path.back() == '/' || path.back() == '\\') ? path.substr(0, path.size() - 1)
                                                                                  : path;
        }

        void ListPictureFolder(const std::string& path, PictureFolder& folder, uint64_t now)
        {
            folder.files.clear();
            folder.checkedAt = now;
            SDL_PathInfo info{};
            folder.modified = SDL_GetPathInfo(Bare(path).c_str(), &info) ? info.modify_time : 0;
            SDL_Time clock = 0;
            folder.settled = !SDL_GetCurrentTime(&clock) || clock - folder.modified >= PictureFolderSettleNs;
            g_pictureFolderListings++;
            if (info.type != SDL_PATHTYPE_DIRECTORY)
                return;
            SDL_EnumerateDirectory(
                Bare(path).c_str(),
                [](void* data, const char*, const char* name) {
                    std::string file = name;
                    if (file.size() > 4 && Lower(file.substr(file.size() - 4)) == ".png")
                        static_cast<PictureFolder*>(data)->files.emplace(Lower(file.substr(0, file.size() - 4)), file);
                    return SDL_ENUM_CONTINUE;
                },
                &folder);
        }

        // The folder's listing, read again when the folder changed since (checked at most once a second): a picture
        // another process adds is found within a second.
        PictureFolder& CachedPictureFolder(const std::string& path)
        {
            uint64_t now = SDL_GetTicks();
            auto [it, added] = g_pictureFolders.try_emplace(path);
            PictureFolder& folder = it->second;
            if (added)
                ListPictureFolder(path, folder, now);
            else if (now - folder.checkedAt >= PictureFolderCheckMs)
            {
                SDL_PathInfo info{};
                SDL_Time modified = SDL_GetPathInfo(Bare(path).c_str(), &info) ? info.modify_time : 0;
                if (modified != folder.modified || !folder.settled)
                    ListPictureFolder(path, folder, now);
                else
                    folder.checkedAt = now;
            }
            return folder;
        }

        // SKY_PORTRAITS, else <data>/<Game>/portraits; remembered per data root (the path costs an environment read, a
        // working-folder lookup and, on macOS, a user database lookup).
        std::string PortraitFolder(const Game& game)
        {
            if (const char* portraits = skyenv::Get("SKY_PORTRAITS"))
                return std::string(portraits) + "/";
            const char* data = skyenv::Get("SKYLANDERS_DATA");
            std::string key = game.id + "\n" + (data != nullptr ? data : "");
            if (auto found = g_portraitFolders.find(key); found != g_portraitFolders.end())
                return found->second;
            std::filesystem::path root = datadir::Game(game.id);
            std::string folder = root.empty() ? std::string() : datadir::Text(root / "portraits") + "/";
            return g_portraitFolders[key] = folder;
        }

        // The first of `names` in `folder` (with its trailing separator), as listed; the caller holds
        // g_pictureFoldersLock. A name may start with a subfolder ("artworks/...").
        std::string FirstListed(const std::string& folder, const std::vector<std::string>& names)
        {
            for (const std::string& name : names)
            {
                size_t slash = name.rfind('/');
                std::string sub = slash == std::string::npos ? std::string() : name.substr(0, slash + 1);
                PictureFolder& listed = CachedPictureFolder(folder + sub);
                // Any case: the disc's names and the files may differ on a case-insensitive system, as a stat allowed.
                auto file = listed.files.find(Lower(name.substr(sub.size())));
                if (file == listed.files.end())
                    continue;
                // One stat for the picture found: one deleted since the folder was read is never handed out.
                std::string path = folder + sub + file->second;
                if (SDL_GetPathInfo(path.c_str(), nullptr))
                    return path;
                ListPictureFolder(folder + sub, listed, SDL_GetTicks());
            }
            return {};
        }

        // The first of `names` cached in the game's portraits folder (SKY_PORTRAITS, else <data>/<Game>/portraits).
        std::string FirstCached(const Game& game, const std::vector<std::string>& names)
        {
            std::lock_guard<std::mutex> lock(g_pictureFoldersLock);
            std::string folder = PortraitFolder(game);
            return folder.empty() ? std::string() : FirstListed(folder, names);
        }

        // pictures/pictures.json as last read: {"default": "origin" | "newest" | <game id>, "cards": {"<type>:<variant>":
        // <game id> | "own"}, "revision": <n>}. Looked at again at most once a second, for another process's changes.
        struct PictureSettings
        {
            std::string folder; // the collection folder it was read from
            std::string fallback = PicturesOrigin;
            std::map<uint32_t, std::string> cards; // type << 16 | variant -> a game id or PictureOwn
            int64_t revision = 0;
            SDL_Time modified = 0;
            uint64_t checkedAt = 0;
            bool read = false, settled = false;
            bool recheck = false; // ForgetPictureFolders: looked at again on the next call

            bool SameChoices(const PictureSettings& other) const
            {
                return fallback == other.fallback && cards == other.cards && revision == other.revision;
            }
        };

        std::mutex g_pictureSettingsLock;
        PictureSettings g_pictureSettings;
        std::atomic<uint64_t> g_pictureGeneration{ 1 };

        std::string PicturesFolder(const std::string& collection) { return collection + "/pictures"; }
        std::string SettingsPath(const std::string& collection)
        {
            return PicturesFolder(collection) + "/pictures.json";
        }
        std::string OwnName(uint16_t type, uint16_t variant)
        {
            return std::to_string(type) + "-" + std::to_string(variant);
        }

        void ParseSettings(const std::string& text, PictureSettings& settings)
        {
            settings.fallback = PicturesOrigin;
            settings.cards.clear();
            settings.revision = 0;
            Json json;
            std::string error;
            if (!Json::Parse(text, json, error) || !json.IsObject())
                return;
            if (json["default"].IsString() && !json["default"].AsString().empty())
                settings.fallback = json["default"].AsString();
            settings.revision = json["revision"].AsInt();
            for (const auto& [key, value] : json["cards"].Members())
            {
                uint16_t type = 0, variant = 0;
                const char* end = key.data() + key.size();
                auto [colon, typeError] = std::from_chars(key.data(), end, type);
                if (typeError != std::errc() || colon == end || *colon != ':')
                    continue;
                auto [last, variantError] = std::from_chars(colon + 1, end, variant);
                if (variantError == std::errc() && last == end && value.IsString() && !value.AsString().empty())
                    settings.cards[uint32_t(type) << 16 | variant] = value.AsString();
            }
        }

        // Reads the file again into g_pictureSettings (the caller holds g_pictureSettingsLock); a change of the choices
        // moves the generation on.
        void ReadSettings(const std::string& collection, uint64_t now)
        {
            PictureSettings fresh;
            fresh.folder = collection;
            fresh.checkedAt = now;
            fresh.read = true;
            const std::string path = SettingsPath(collection);
            SDL_PathInfo info{};
            if (SDL_GetPathInfo(path.c_str(), &info))
            {
                fresh.modified = info.modify_time;
                size_t size = 0;
                if (void* bytes = SDL_LoadFile(path.c_str(), &size))
                {
                    ParseSettings(std::string(static_cast<const char*>(bytes), size), fresh);
                    SDL_free(bytes);
                }
            }
            SDL_Time clock = 0;
            fresh.settled = !SDL_GetCurrentTime(&clock) || clock - fresh.modified >= PictureFolderSettleNs;
            // Only a change moves it on: a cache dropped on every move would be dropped again and again (the launcher
            // forgets the folders when it drops its pictures).
            if (!g_pictureSettings.read || g_pictureSettings.folder != collection ||
                !g_pictureSettings.SameChoices(fresh))
                g_pictureGeneration++;
            g_pictureSettings = std::move(fresh);
        }

        // The settings, looked at again when a second has passed (the caller holds g_pictureSettingsLock).
        const PictureSettings& CurrentSettings()
        {
            const uint64_t now = SDL_GetTicks();
            PictureSettings& settings = g_pictureSettings;
            if (settings.read && !settings.recheck && now - settings.checkedAt < PictureFolderCheckMs)
                return settings;
            const std::string collection = Folder();
            if (!settings.read || settings.recheck || settings.folder != collection)
            {
                ReadSettings(collection, now);
                return settings;
            }
            SDL_PathInfo info{};
            SDL_Time modified = SDL_GetPathInfo(SettingsPath(collection).c_str(), &info) ? info.modify_time : 0;
            if (modified != settings.modified || !settings.settled)
                ReadSettings(collection, now);
            else
                settings.checkedAt = now;
            return settings;
        }

        // Applies `change` to the file as it is now (another process may have written it) and writes it back whole.
        template <typename Change>
        bool UpdateSettings(Change change)
        {
            std::lock_guard<std::mutex> lock(g_pictureSettingsLock);
            const std::string collection = Folder();
            const std::string path = SettingsPath(collection);
            Json json = Json::Object();
            size_t size = 0;
            if (void* bytes = SDL_LoadFile(path.c_str(), &size))
            {
                std::string error;
                Json parsed;
                if (Json::Parse(std::string(static_cast<const char*>(bytes), size), parsed, error) && parsed.IsObject())
                    json = std::move(parsed);
                SDL_free(bytes);
            }
            Json cards = json["cards"].IsObject() ? json["cards"] : Json::Object();
            std::string fallback = json["default"].IsString() ? json["default"].AsString() : std::string();
            change(fallback, cards);
            json.Set("format", 1);
            json.Set("revision", json["revision"].AsInt() + 1);
            if (fallback.empty() || fallback == PicturesOrigin)
                json.Erase("default");
            else
                json.Set("default", fallback);
            json.Set("cards", std::move(cards));
            SDL_CreateDirectory(PicturesFolder(collection).c_str());
            // Written beside and renamed over: the other app never reads half a file.
            const std::string temporary = path + ".part";
            const std::string text = json.Dump();
            bool saved = SDL_SaveFile(temporary.c_str(), text.data(), text.size()) &&
                         SDL_RenamePath(temporary.c_str(), path.c_str());
            if (!saved)
                SDL_RemovePath(temporary.c_str());
            ReadSettings(collection, SDL_GetTicks());
            return saved;
        }

        std::string CardKey(uint16_t type, uint16_t variant)
        {
            return std::to_string(type) + ":" + std::to_string(variant);
        }

        // The own picture's file when the card's choice is PictureOwn and the file is there.
        std::string OwnPictureOf(const PictureSettings& settings, uint16_t type, uint16_t variant)
        {
            auto choice = settings.cards.find(uint32_t(type) << 16 | variant);
            if (choice == settings.cards.end() || choice->second != PictureOwn)
                return {};
            std::lock_guard<std::mutex> lock(g_pictureFoldersLock);
            return FirstListed(PicturesFolder(settings.folder) + "/", { OwnName(type, variant) });
        }
    }

    std::string PicturesDefault()
    {
        std::lock_guard<std::mutex> lock(g_pictureSettingsLock);
        return CurrentSettings().fallback;
    }

    bool SetPicturesDefault(const std::string& choice)
    {
        return UpdateSettings([&](std::string& fallback, Json&) { fallback = choice; });
    }

    std::string PictureChoice(uint16_t type, uint16_t variant)
    {
        std::lock_guard<std::mutex> lock(g_pictureSettingsLock);
        const PictureSettings& settings = CurrentSettings();
        auto choice = settings.cards.find(uint32_t(type) << 16 | variant);
        if (choice == settings.cards.end())
            return {};
        if (choice->second == PictureOwn && OwnPictureOf(settings, type, variant).empty())
            return {};
        return choice->second;
    }

    bool SetPictureChoice(uint16_t type, uint16_t variant, const std::string& choice)
    {
        if (choice == PictureOwn)
            return false;
        return UpdateSettings([&](std::string&, Json& cards) {
            if (choice.empty())
                cards.Erase(CardKey(type, variant));
            else
                cards.Set(CardKey(type, variant), choice);
        });
    }

    bool SetOwnPicture(uint16_t type, uint16_t variant, const Image& image, std::string* error)
    {
        auto refuse = [&](const char* why) {
            if (error != nullptr)
                *error = why;
            return false;
        };
        Image visible = CropToVisible(image);
        if (visible.Empty() || visible.width <= 0 || visible.height <= 0)
            return refuse("the picture is empty");
        // Square, the picture fitted in the middle: the round portraits and the cards draw a square.
        const int side = std::min(OwnPictureSize, std::max(visible.width, visible.height));
        const float scale = float(side) / float(std::max(visible.width, visible.height));
        const int width = std::max(1, int(std::lround(visible.width * scale)));
        const int height = std::max(1, int(std::lround(visible.height * scale)));
        Image fitted = Resize(visible, width, height);
        Image square{ side, side, std::vector<uint32_t>(size_t(side) * size_t(side), 0) };
        const int x0 = (side - width) / 2, y0 = (side - height) / 2;
        for (int y = 0; y < height; y++)
            std::copy_n(fitted.rgba.begin() + size_t(y) * size_t(width), width,
                square.rgba.begin() + size_t(y + y0) * size_t(side) + size_t(x0));
        const std::string folder = PicturesFolder(Folder());
        SDL_CreateDirectory(folder.c_str());
        const std::string path = folder + "/" + OwnName(type, variant) + ".png";
        const std::string temporary = path + ".part";
        if (!SavePng(square, temporary) || !SDL_RenamePath(temporary.c_str(), path.c_str()))
        {
            SDL_RemovePath(temporary.c_str());
            return refuse("the picture cannot be saved in the collection folder");
        }
        ForgetPictureFolders();
        if (!UpdateSettings([&](std::string&, Json& cards) { cards.Set(CardKey(type, variant), PictureOwn); }))
            return refuse("the collection's picture choices cannot be saved");
        return true;
    }

    bool RemoveOwnPicture(uint16_t type, uint16_t variant)
    {
        const std::string path = PicturesFolder(Folder()) + "/" + OwnName(type, variant) + ".png";
        bool removed = !SDL_GetPathInfo(path.c_str(), nullptr) || SDL_RemovePath(path.c_str());
        ForgetPictureFolders();
        bool saved = UpdateSettings([&](std::string&, Json& cards) {
            if (cards[CardKey(type, variant)].AsString() == PictureOwn)
                cards.Erase(CardKey(type, variant));
        });
        return removed && saved;
    }

    std::string OwnPicturePath(uint16_t type, uint16_t variant)
    {
        std::lock_guard<std::mutex> lock(g_pictureSettingsLock);
        return OwnPictureOf(CurrentSettings(), type, variant);
    }

    uint64_t PictureGeneration()
    {
        std::lock_guard<std::mutex> lock(g_pictureSettingsLock);
        CurrentSettings();
        return g_pictureGeneration.load();
    }

    void ForgetPictureFolders()
    {
        {
            std::lock_guard<std::mutex> lock(g_pictureFoldersLock);
            g_pictureFolders.clear();
            g_portraitFolders.clear();
        }
        std::lock_guard<std::mutex> lock(g_pictureSettingsLock);
        g_pictureSettings.recheck = true; // the collection folder may have moved (tests, a profile)
    }

    size_t PictureFolderListings()
    {
        std::lock_guard<std::mutex> lock(g_pictureFoldersLock);
        return g_pictureFolderListings;
    }

    std::string GamePortraitPath(const std::string& gameId, const Toy& toy, uint16_t variant, bool large)
    {
        const Game* game = Catalogue::Get().FindGame(gameId);
        if (game == nullptr)
            return {};
        // The game's names for its pictures: Giants lists a file per toy and deco; Spyro's Adventure names its characters
        // ("toy_<name>_hud", 128 px; "<name>_vs", 512 px) and a few other pictures (items).
        std::vector<std::string> names;
        if (auto it = game->portraits.find(uint32_t(toy.type) << 16 | variant);
            variant != 0 && it != game->portraits.end())
            names.push_back(it->second);
        if (auto it = game->portraits.find(uint32_t(toy.type) << 16); it != game->portraits.end())
            names.push_back(it->second);
        if (auto it = game->names.find(toy.type); it != game->names.end())
        {
            if (large)
                names.push_back(Lower(it->second) + "_vs");
            names.push_back("toy_" + Lower(it->second) + "_hud");
        }
        else if (auto image = game->images.find(toy.type); image != game->images.end())
            names.push_back(image->second);
        if (names.empty())
            return {};
        return FirstCached(*game, names);
    }

    std::string VillainPicturePath(const std::string& gameId, const Villain& villain, bool captured)
    {
        const Game* game = Catalogue::Get().FindGame(gameId);
        if (game == nullptr)
            return {};
        return FirstCached(*game, { std::to_string(villain.id) + (captured ? "_villaincaptured" : "_villainescaped") });
    }

    std::string GameKindPicturePath(const std::string& gameId, Category category)
    {
        const Game* game = Catalogue::Get().FindGame(gameId);
        if (game == nullptr)
            return {};
        auto found = game->kindPictures.find(category);
        return found != game->kindPictures.end() ? FirstCached(*game, { found->second }) : std::string();
    }

    namespace
    {
        // icons/<key> in `gameId`'s portraits folder, else in the newest game's.
        std::string CachedIcon(const std::string& gameId, const std::string& key)
        {
            const Catalogue& catalogue = Catalogue::Get();
            if (const Game* game = catalogue.FindGame(gameId))
                if (std::string path = FirstCached(*game, { "icons/" + key }); !path.empty())
                    return path;
            std::vector<const Game*> newest;
            for (const Game& game : catalogue.Games())
                newest.push_back(&game);
            std::stable_sort(
                newest.begin(), newest.end(), [](const Game* a, const Game* b) { return a->year > b->year; });
            for (const Game* game : newest)
                if (std::string path = game->id != gameId ? FirstCached(*game, { "icons/" + key }) : std::string();
                    !path.empty())
                    return path;
            return {};
        }
    }

    std::string ElementIconPath(const std::string& gameId, Element element)
    {
        if (element >= Element::None)
            return {};
        return CachedIcon(gameId, std::string("element_") + ElementId(element));
    }

    std::string ElementIconPath(const std::string& gameId, const Toy& toy)
    {
        if (!toy.elementIcon.empty())
            return CachedIcon(gameId, "element_" + toy.elementIcon);
        // The item families, which have no element: their own mark.
        if (toy.category == Category::MagicItem)
            return CachedIcon(gameId, "element_magicitem");
        if (toy.category == Category::Adventure)
            return CachedIcon(gameId, "element_adventure");
        return ElementIconPath(gameId, toy.element);
    }

    std::string TypeIconPath(const std::string& gameId, const Toy& toy)
    {
        if (!toy.swapType.empty())
            return CachedIcon(gameId, "swap_" + toy.swapType);
        if (!toy.ride.empty())
            return CachedIcon(gameId, "ride_" + toy.ride);
        switch (toy.category)
        {
        case Category::TrapMaster: return CachedIcon(gameId, "type_trapmaster");
        case Category::Giant: return CachedIcon(gameId, "type_giant");
        case Category::Mini: return CachedIcon(gameId, "type_mini");
        default: break;
        }
        return ClassIconPath(gameId, toy.battleClass);
    }

    std::string ClassIconPath(const std::string& gameId, int battleClass)
    {
        std::string name = battleClass == 11 ? "Kaos" : figures::BattleClassName(battleClass);
        if (name.empty())
            return {};
        return CachedIcon(gameId, "class_" + Lower(name));
    }

    namespace
    {
        enum class PictureKind
        {
            Portrait,
            Best,
            Half,
            JoinedHalf
        };

        // The games to look in, in order (icons.h, "The shared picture rule"), and the card's own choices.
        struct PictureOrder
        {
            std::vector<const Game*> games;
            const Game* chosen = nullptr; // the card's chosen game, looked in first, plain art included
            std::string own; // the card's own picture
        };

        PictureOrder OrderFor(const Toy& toy, uint16_t variant)
        {
            const Catalogue& catalogue = Catalogue::Get();
            const auto& all = catalogue.Games();
            PictureOrder order;
            std::string fallback, card;
            {
                std::lock_guard<std::mutex> lock(g_pictureSettingsLock);
                const PictureSettings& settings = CurrentSettings();
                fallback = settings.fallback;
                if (auto found = settings.cards.find(uint32_t(toy.type) << 16 | variant); found != settings.cards.end())
                    card = found->second;
                if (card == PictureOwn)
                    order.own = OwnPictureOf(settings, toy.type, variant);
            }
            order.games.reserve(all.size());
            auto add = [&](const Game* game) {
                if (game != nullptr && std::find(order.games.begin(), order.games.end(), game) == order.games.end())
                    order.games.push_back(game);
            };
            if (card != PictureOwn)
                order.chosen = catalogue.FindGame(card);
            if (fallback == PicturesNewest)
                for (auto game = all.rbegin(); game != all.rend(); ++game)
                    add(&*game);
            add(catalogue.FindGame(fallback));
            const Game* origin = catalogue.Origin(toy, variant);
            if (origin == nullptr) // a toy of no known game: the newest first, as before there was a choice
                for (auto game = all.rbegin(); game != all.rend(); ++game)
                    add(&*game);
            const size_t at = origin != nullptr ? size_t(origin - all.data()) : 0;
            for (size_t i = at; i < all.size(); i++)
                add(&all[i]);
            for (size_t i = at; i-- > 0;)
                add(&all[i]);
            return order;
        }

        // The pictures of `games`, in that order: a variant's own art before plain art. Half images take precedence for
        // swappers; whole-figure art is never used in a waist composite.
        std::string PictureIn(
            const Toy& toy, uint16_t variant, PictureKind kind, bool large, const std::vector<const Game*>& games)
        {
            auto mapped = [](const Game& game, const auto& pictures, uint16_t type, uint16_t version,
                              const char* folder = "") -> std::string {
                auto it = pictures.find(uint32_t(type) << 16 | version);
                return it == pictures.end() ? std::string() : FirstCached(game, { folder + it->second });
            };
            bool half = toy.category == Category::SwapTop || toy.category == Category::SwapBottom;
            const Toy* whole =
                toy.category == Category::SwapBottom ? Catalogue::Get().Find(MatchingHalf(toy.type)) : nullptr;
            for (int pass = 0; pass < (variant != 0 ? 2 : 1); ++pass)
            {
                uint16_t version = pass == 0 ? variant : 0;
                if (half)
                    for (const Game* game : games)
                        if (std::string path = mapped(*game, game->halves, toy.type, version, "artworks/");
                            !path.empty())
                            return path;
                if (kind == PictureKind::JoinedHalf)
                    continue;
                for (const Game* game : games)
                    for (const Toy* candidate : { &toy, whole })
                    {
                        if (candidate == nullptr)
                            continue;
                        if (kind != PictureKind::Portrait)
                            if (std::string art = mapped(*game, game->artworks, candidate->type, version, "artworks/");
                                !art.empty())
                                return art;
                        if (std::string portrait = mapped(*game, game->portraits, candidate->type, version);
                            !portrait.empty())
                            return portrait;
                        // Unkeyed names and images describe the plain toy only.
                        if (version == 0)
                            if (std::string plain = GamePortraitPath(game->id, *candidate, 0, large); !plain.empty())
                                return plain;
                        if (kind == PictureKind::Portrait)
                            if (std::string art = mapped(*game, game->artworks, candidate->type, version, "artworks/");
                                !art.empty())
                                return art;
                    }
            }
            return {};
        }

        std::string PicturePath(const Toy& toy, uint16_t variant, PictureKind kind, bool large)
        {
            bool half = toy.category == Category::SwapTop || toy.category == Category::SwapBottom;
            if (!half && (kind == PictureKind::Half || kind == PictureKind::JoinedHalf))
                return {};
            const PictureOrder order = OrderFor(toy, variant);
            if (!order.own.empty())
                return order.own;
            if (order.chosen != nullptr)
                if (std::string path = PictureIn(toy, variant, kind, large, { order.chosen }); !path.empty())
                    return path;
            if (std::string path = PictureIn(toy, variant, kind, large, order.games); !path.empty())
                return path;
            if (kind == PictureKind::JoinedHalf)
                return {};
            for (const Game* game : order.games)
                if (std::string path = GameKindPicturePath(game->id, toy.category); !path.empty())
                    return path;
            return {};
        }
    }

    std::string PortraitPath(const std::string& /*gameId*/, const Toy& toy, uint16_t variant, bool large)
    {
        return PicturePath(toy, variant, PictureKind::Portrait, large);
    }

    std::string BestPicturePath(const std::string& /*gameId*/, const Toy& toy, uint16_t variant)
    {
        return PicturePath(toy, variant, PictureKind::Best, true);
    }

    std::string HalfPicturePath(const Toy& toy, uint16_t variant)
    {
        return PicturePath(toy, variant, PictureKind::Half, true);
    }

    std::vector<std::string> PictureGames(const Toy& toy, uint16_t variant)
    {
        std::vector<std::string> games;
        for (const Game& game : Catalogue::Get().Games())
            if (!PictureIn(toy, variant, PictureKind::Best, true, { &game }).empty())
                games.push_back(game.id);
        return games;
    }

    namespace
    {
        // The bounding box (x0, y0, x1, y1: end exclusive) of the pixels whose alpha is above `minAlpha`.
        bool VisibleBox(const Image& image, uint8_t minAlpha, int& x0, int& y0, int& x1, int& y1)
        {
            x0 = image.width, y0 = image.height, x1 = 0, y1 = 0;
            for (int y = 0; y < image.height; y++)
                for (int x = 0; x < image.width; x++)
                    if ((image.rgba[size_t(y) * image.width + x] >> 24) > minAlpha)
                        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x + 1), y1 = std::max(y1, y + 1);
            return x0 < x1 && y0 < y1;
        }

        Image Cut(const Image& image, int x0, int y0, int x1, int y1)
        {
            Image out{ x1 - x0, y1 - y0, std::vector<uint32_t>(size_t(x1 - x0) * size_t(y1 - y0)) };
            for (int y = y0; y < y1; y++)
                std::copy_n(image.rgba.begin() + size_t(y) * image.width + x0, x1 - x0,
                    out.rgba.begin() + size_t(y - y0) * out.width);
            return out;
        }

        // `top` composited over `bottom`, both straight alpha.
        uint32_t Over(uint32_t top, uint32_t bottom)
        {
            uint32_t at = top >> 24, ab = bottom >> 24;
            uint32_t a = at * 255 + ab * (255 - at); // alpha x 255
            if (a == 0)
                return 0;
            uint32_t out = ((a + 127) / 255) << 24;
            for (int shift = 0; shift < 24; shift += 8)
                out |= ((((top >> shift) & 255) * at * 255 + ((bottom >> shift) & 255) * ab * (255 - at)) / a) << shift;
            return out;
        }
    }

    Image CropToVisible(const Image& image, uint8_t minAlpha)
    {
        int x0, y0, x1, y1;
        if (!VisibleBox(image, minAlpha, x0, y0, x1, y1))
            return image;
        return Cut(image, x0, y0, x1, y1);
    }

    Image JoinHalves(const Image& top, const Image& bottom)
    {
        int box[4];
        if (!VisibleBox(top, 8, box[0], box[1], box[2], box[3]) ||
            !VisibleBox(bottom, 8, box[0], box[1], box[2], box[3]))
            return {};
        int width = std::max(top.width, bottom.width), height = std::max(top.height, bottom.height);
        Image upper = Resize(top, width, height), lower = Resize(bottom, width, height);
        for (size_t i = 0; i < lower.rgba.size(); i++)
            lower.rgba[i] = Over(upper.rgba[i], lower.rgba[i]);
        if (!VisibleBox(lower, 0, box[0], box[1], box[2], box[3]))
            return {};
        return Cut(lower, box[0], box[1], box[2], box[3]);
    }

    Image SwapperPicture(const Toy& top, uint16_t topVariant, const Toy& bottom, uint16_t bottomVariant)
    {
        // A player's own picture of a half is a whole picture, not one to join.
        if (!OwnPicturePath(top.type, topVariant).empty() || !OwnPicturePath(bottom.type, bottomVariant).empty())
            return {};
        std::string upper = PicturePath(top, topVariant, PictureKind::JoinedHalf, true),
                    lower = PicturePath(bottom, bottomVariant, PictureKind::JoinedHalf, true);
        if (upper.empty() || lower.empty())
            return {};
        // Dedicated variant art must not be replaced by a composite made from plain half images.
        if (PicturePath(top, topVariant, PictureKind::Best, true) != upper ||
            PicturePath(bottom, bottomVariant, PictureKind::Best, true) != lower)
            return {};
        return JoinHalves(LoadPng(upper), LoadPng(lower));
    }

    Image CrystalPicture(const Toy& toy, const std::string& path)
    {
        if (toy.category != Category::CreationCrystal || path.empty())
            return {};
        Image picture = LoadPng(path);
        const uint32_t tint = ElementRgb(toy.element);
        const int tr = int(tint >> 16 & 0xFF), tg = int(tint >> 8 & 0xFF), tb = int(tint & 0xFF);
        for (uint32_t& texel : picture.rgba)
        {
            const int r = int(texel & 0xFF), g = int(texel >> 8 & 0xFF), b = int(texel >> 16 & 0xFF);
            // Magenta: red and blue together well above green. The crystal's gold frame and dark metal are not.
            const int magenta = std::min(r, b) - g;
            if (magenta <= 16 || std::abs(r - b) > 110)
                continue;
            const int t = std::min(256, (magenta - 16) * 256 / 48), shade = std::max(r, b);
            auto mix = [&](int from, int to) {
                const int toned = std::min(255, to * shade / 200);
                return uint32_t((from * (256 - t) + toned * t) >> 8);
            };
            texel = (texel & 0xFF000000) | mix(b, tb) << 16 | mix(g, tg) << 8 | mix(r, tr);
        }
        return picture;
    }

    Image TrapPicture(const Toy& trap, uint16_t variant, uint16_t villain, const std::string& path)
    {
        const Game* game = Catalogue::Get().FindGame(trap.game);
        if (trap.category != Category::Trap || villain == 0 || path.empty() || game == nullptr ||
            !OwnPicturePath(trap.type, variant).empty())
            return {};
        auto held = std::find_if(game->villains.begin(), game->villains.end(),
            [&](const Villain& candidate) { return candidate.id == villain + figures::VillainResourceBase; });
        const std::string villainPath = held != game->villains.end() ? VillainPicturePath(game->id, *held) : "";
        if (villainPath.empty())
            return {};
        Image picture = LoadPng(path), badge = CropToVisible(LoadPng(villainPath));
        if (picture.Empty() || badge.Empty())
            return {};
        // A box of 0.34 of the picture, centred across and at 0.76 down: its corners stay inside the inscribed circle
        // (0.17^2 + 0.43^2 < 0.5^2).
        const int box = std::max(1, std::min(picture.width, picture.height) * 34 / 100);
        const float scale = std::min(float(box) / badge.width, float(box) / badge.height);
        const int w = std::max(1, int(badge.width * scale)), h = std::max(1, int(badge.height * scale));
        badge = Resize(badge, w, h);
        const int x0 = picture.width / 2 - w / 2, y0 = picture.height * 76 / 100 - h / 2;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                uint32_t& texel = picture.rgba[size_t(y0 + y) * picture.width + size_t(x0 + x)];
                texel = Over(badge.rgba[size_t(y) * w + x], texel);
            }
        return picture;
    }

    uint16_t VariantFor(const Toy& toy, VariantKind kind)
    {
        if (kind == VariantKind::Base || kind == toy.edition)
            return 0;
        uint16_t variant = 0;
        int count = 0;
        for (const Variant& candidate : toy.variants)
            if (candidate.kind == kind && candidate.id != 0)
                variant = candidate.id, count++;
        return count == 1 ? variant : 0;
    }

    std::string DiscImagePath(const Toy& toy) { return PortraitPath({}, toy); }

    Image Portrait(
        const std::string& gameId, const Toy* toy, VariantKind kind, int size, uint16_t variant, bool* fromDisc)
    {
        Image image;
        if (toy != nullptr)
        {
            if (variant == 0)
                variant = VariantFor(*toy, kind);
            std::string path = PortraitPath(gameId, *toy, variant, size > 128);
            if (!path.empty())
                image = toy->category == Category::CreationCrystal ? CrystalPicture(*toy, path) : LoadPng(path);
        }
        if (fromDisc != nullptr)
            *fromDisc = !image.Empty();
        image = image.Empty() ? GeneratedIcon(toy, size) : Resize(image, size, size);
        AddBadge(image, kind);
        return image;
    }

    Image Icon(const Toy* toy, VariantKind kind, int size, bool* fromDisc)
    {
        return Portrait({}, toy, kind, size, 0, fromDisc);
    }

    Image LoadPng(const std::string& path)
    {
        Image image;
        SDL_Surface* loaded = SDL_LoadPNG(path.c_str());
        if (loaded == nullptr)
            return image;
        if (SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32))
        {
            image.width = rgba->w;
            image.height = rgba->h;
            image.rgba.resize(size_t(rgba->w) * rgba->h);
            for (int y = 0; y < rgba->h; y++)
                memcpy(&image.rgba[size_t(y) * rgba->w],
                    static_cast<const uint8_t*>(rgba->pixels) + size_t(y) * rgba->pitch, size_t(rgba->w) * 4);
            SDL_DestroySurface(rgba);
        }
        SDL_DestroySurface(loaded);
        return image;
    }

    bool SavePng(const Image& image, const std::string& path)
    {
        SDL_Surface* surface = SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_RGBA32,
            const_cast<uint32_t*>(image.rgba.data()), image.width * 4);
        bool saved = surface != nullptr && SDL_SavePNG(surface, path.c_str());
        SDL_DestroySurface(surface);
        if (saved)
            ForgetPictureFolders(); // the next lookup finds it
        return saved;
    }

    Image Resize(const Image& image, int width, int height)
    {
        if (image.width == width && image.height == height)
            return image;
        // Box filter when shrinking, bilinear when growing; colours weighted by alpha.
        Image out{ width, height, std::vector<uint32_t>(size_t(width) * height) };
        float sx = float(image.width) / width, sy = float(image.height) / height;
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                float u0 = x * sx, v0 = y * sy, u1 = u0 + std::max(sx, 1.0f), v1 = v0 + std::max(sy, 1.0f);
                if (sx < 1)
                {
                    u0 = std::max(0.0f, (x + 0.5f) * sx - 0.5f);
                    u1 = u0 + 1;
                }
                if (sy < 1)
                {
                    v0 = std::max(0.0f, (y + 0.5f) * sy - 0.5f);
                    v1 = v0 + 1;
                }
                float sum[4] = {}, weight = 0;
                for (int v = int(v0); v < int(std::ceil(v1)) && v < image.height; v++)
                {
                    float wy = std::min(v1, v + 1.0f) - std::max(v0, float(v));
                    for (int u = int(u0); u < int(std::ceil(u1)) && u < image.width; u++)
                    {
                        float w = wy * (std::min(u1, u + 1.0f) - std::max(u0, float(u)));
                        Rgba c = Unpack(image.rgba[size_t(v) * image.width + u]);
                        sum[0] += c.r * c.a * w;
                        sum[1] += c.g * c.a * w;
                        sum[2] += c.b * c.a * w;
                        sum[3] += c.a * w;
                        weight += w;
                    }
                }
                Rgba c = { 0, 0, 0, 0 };
                if (sum[3] > 0)
                    c = { sum[0] / sum[3], sum[1] / sum[3], sum[2] / sum[3], sum[3] / weight };
                out.rgba[size_t(y) * width + x] = Pack(c);
            }
        }
        return out;
    }
}
