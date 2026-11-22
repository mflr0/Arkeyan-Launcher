// The collection page: the portal at the next start along the top (the chosen game's seats), the bar (search, Element,
// Game, Type), the shelf below (every figure once, favorites first). The figures and their folder are the shared
// library's (src/shared/collection); the model is collection_model.cpp; the figure sheet, Add a figure and Manage are
// in collection_figure.cpp, collection_add.cpp and collection_manage.cpp.
#include "launcher/ui/launcher_app.h"
#include "launcher/env.h"
#include "shared/collection/datadir.h"
#include "launcher/ui/launcher_ui.h"
#include "launcher/ui/seat_rows.h"
#include "shared/collection/icons.h"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fmt/format.h>
#include <imgui_internal.h> // the nav id of the search field

namespace launcher_ui
{
    namespace fs = std::filesystem;
    using collection::Category;
    using collection::Figure;
    using collection::Section;

    // ---- Helpers --------------------------------------------------------------------------------------------------

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(tolower(c)); });
        return text;
    }

    std::string PlayTime(uint32_t seconds)
    {
        return seconds >= 3600 ? Tf("{} h {:02} min", seconds / 3600, seconds / 60 % 60) : Tf("{} min", seconds / 60);
    }

    std::string When(const std::string& iso)
    {
        const std::time_t when = ui::ParseIsoUtc(iso);
        return when == 0 ? iso : ui::FormatWhen(when, std::time(nullptr));
    }

    std::string ElementName(collection::Element element)
    {
        static const char* const Names[] = { N_("Air"), N_("Earth"), N_("Fire"), N_("Water"), N_("Magic"), N_("Tech"),
            N_("Life"), N_("Undead"), N_("Light"), N_("Dark"), N_("No element") };
        return Tr(Names[std::min<size_t>(size_t(element), 10)]);
    }

    std::string CategoryLabel(collection::Category category)
    {
        static const char* const Names[] = { N_("Skylanders"), N_("Giants"), N_("Swap Force tops"),
            N_("Swap Force bottoms"), N_("Trap Masters"), N_("SuperChargers"), N_("Senseis"), N_("Minis and sidekicks"),
            N_("Traps"), N_("Villains"), N_("Vehicles"), N_("Creation Crystals"), N_("Magic Items"),
            N_("Adventure Packs"), N_("Trophies") };
        static_assert(std::size(Names) == size_t(collection::Category::Count));
        return category < collection::Category::Count ? Tr(Names[size_t(category)]) : std::string();
    }

    std::string KindLabel(collection::VariantKind kind)
    {
        static const char* const Names[] = { N_("Standard"), N_("Legendary"), N_("Dark"), N_("Crystal Clear"),
            N_("Glow in the Dark"), N_("Metallic"), N_("Gold"), N_("Silver"), N_("Chrome"), N_("Flocked"), N_("Series"),
            N_("LightCore"), N_("Eon's Elite"), N_("Alternate deco"), N_("Event and exclusive"), N_("Trap shape"),
            N_("Other") };
        static_assert(std::size(Names) == size_t(collection::VariantKind::Count));
        return kind < collection::VariantKind::Count ? Tr(Names[size_t(kind)]) : std::string();
    }

    std::string VersionSubtitle(const collection::Toy& toy, size_t version)
    {
        const collection::Variant* variant = version == 0 ? nullptr : &toy.variants[version - 1];
        std::string text = KindLabel(variant == nullptr ? toy.edition : variant->kind);
        // A same-tag edition has no id of its own: it came with the toy's game, as the plain toy.
        const uint16_t id = variant == nullptr ? collection::StandardVariant(toy) : variant->sameTag ? 0 : variant->id;
        if (const collection::Game* origin = collection::Catalogue::Get().Origin(toy, id))
            text += " · " + ShortGameName(origin->id);
        return text;
    }

    bool French() { return i18n::Current() == "fr"; }

    namespace
    {
        // One piece of the library's English level line ("Level 10 (15)", "Shield 2", "Virtual", a crystal's class, a
        // category's title...), in the
        // launcher's language. A piece it does not know stays as it is (a name).
        std::string LevelPiece(const std::string& piece)
        {
            static const char* const Names[] = { N_("Knight"), N_("Bowslinger"), N_("Quickshot"), N_("Ninja"),
                N_("Brawler"), N_("Smasher"), N_("Sorcerer"), N_("Swashbuckler"), N_("Sentinel"), N_("Bazooker"),
                N_("Giant"), N_("Swapper"), N_("Trap Master"), N_("SuperCharger"), N_("Sensei"), N_("Mini") };
            int level = 0, shown = 0;
            if (std::sscanf(piece.c_str(), "Level %d (%d)", &level, &shown) == 2)
                return Tf("Level {} ({})", level, shown);
            if (std::sscanf(piece.c_str(), "Level %d", &level) == 1)
                return Tf("Level {}", level);
            if (std::sscanf(piece.c_str(), "Shield %d", &level) == 1)
                return Tf("Shield {}", level);
            if (std::sscanf(piece.c_str(), "Weapon %d", &level) == 1)
                return Tf("Weapon {}", level);
            if (piece == "Virtual")
                return Tr("Virtual");
            if (piece == "Ready to create")
                return Tr("Ready to create");
            if (piece == "Unreadable")
                return Tr("Unreadable");
            if (piece == "New")
                return Tr("New");
            for (const char* name : Names)
                if (piece == name)
                    return Tr(name);
            return piece;
        }

        std::string LevelPieces(const std::string& english)
        {
            std::string out;
            for (size_t begin = 0; begin <= english.size();)
            {
                constexpr std::string_view Dot = " \xC2\xB7 ";
                size_t end = english.find(Dot, begin);
                if (end == std::string::npos)
                    end = english.size();
                if (!out.empty())
                    out += Dot;
                out += LevelPiece(english.substr(begin, end - begin));
                begin = end + Dot.size();
            }
            return out;
        }
    }

    // The library words these in English.
    std::string LevelLabel(const collection::Figure& figure, const std::string& game)
    {
        return LevelPieces(collection::LevelText(figure, game));
    }

    std::string CardLevelLabel(const collection::Card& card, const std::string& game)
    {
        return LevelPieces(collection::CardLevelText(card, game));
    }

    std::string SeatTagText(const collection::Seat& seat)
    {
        if (seat.id.size() == 2 && seat.id[0] == 'p')
            return Tf("P{}", seat.id.substr(1));
        if (seat.id == "item")
            return Tr("Item");
        if (seat.id == "adventure")
            return Tr("Adventure");
        if (seat.id == "trap")
            return Tr("Trap");
        return collection::SeatTag(seat);
    }

    std::string SortText(collection::Sort sort)
    {
        switch (sort)
        {
        case collection::Sort::Category: return Tr("Category");
        case collection::Sort::Recent: return Tr("Recent");
        case collection::Sort::Name: return Tr("Name");
        case collection::Sort::Level: return Tr("Level");
        case collection::Sort::Element: return Tr("Element");
        case collection::Sort::Game: return Tr("Game");
        case collection::Sort::Favorites: return Tr("Favorites");
        default: return {};
        }
    }

    std::string FigureCountText(int count)
    {
        // French counts 0 as one.
        return count == 1 || (count == 0 && French()) ? Tf("{} figure", count) : Tf("{} figures", count);
    }

    unsigned ElementInk(collection::Element element)
    {
        if (size_t(element) < 8)
            return Elements[size_t(element)];
        return element == collection::Element::Light  ? 0xE9D96A
               : element == collection::Element::Dark ? 0x7A6AA8
                                                      : 0x8A8A96;
    }

    namespace
    {
        std::vector<std::string> SplitList(const std::string& list)
        {
            std::vector<std::string> items;
            for (size_t begin = 0; begin < list.size();)
            {
                size_t end = std::min(list.find(',', begin), list.size());
                if (end > begin)
                    items.push_back(list.substr(begin, end - begin));
                begin = end + 1;
            }
            return items;
        }

        // "AB" for "Anvil Rain", "G" for "Gill"...: the letters written over a generated icon.
        std::string Initials(const std::string& name)
        {
            std::string out;
            bool start = true;
            for (char c : name)
            {
                if (out.size() >= 2)
                    break;
                if (start && isalpha(static_cast<unsigned char>(c)))
                    out += char(toupper(static_cast<unsigned char>(c)));
                start = c == ' ' || c == '-';
            }
            return out;
        }

        // The elements in the order the games' menus show them (the Element menu).
        constexpr collection::Element MenuElements[] = { collection::Element::Magic, collection::Element::Earth,
            collection::Element::Water, collection::Element::Air, collection::Element::Fire, collection::Element::Life,
            collection::Element::Undead, collection::Element::Tech, collection::Element::Light,
            collection::Element::Dark };
    }

    // ---- Icons ----------------------------------------------------------------------------------------------------

    CollectionPage::CollectionPage(App& app) : m_app(app), m_icons(app.m_figureIcons) {}

    FigureIconCache::~FigureIconCache() { Clear(); }

    void FigureIconCache::Clear()
    {
        for (auto& [name, icon] : textures)
            if (icon.texture != nullptr)
                SDL_DestroyTexture(icon.texture);
        textures.clear();
    }

    void CollectionPage::ClearIcons()
    {
        m_icons->Clear();
        m_app.m_pictures.Forget(FigurePictureKey); // what was still being read is read again, from the disc's pictures
        collection::ForgetPictureFolders(); // the portraits folders were just written
    }

    namespace
    {
        DecodedPicture FromImage(const collection::Image& image)
        {
            DecodedPicture picture;
            if (image.Empty())
                return picture;
            picture.rgba.resize(image.rgba.size() * 4);
            std::memcpy(picture.rgba.data(), image.rgba.data(),
                picture.rgba.size()); // R, G, B, A in memory on the little-endian systems we run on
            picture.width = image.width, picture.height = image.height;
            return picture;
        }

        // A file's picture; a creation crystal's is the disc's one magenta picture, tinted per element (CrystalPicture),
        // a trap's has the villain it holds at its bottom (TrapPicture).
        DecodedPicture FilePicture(
            const collection::Toy& toy, const std::string& path, uint16_t variant = 0, uint16_t villain = 0)
        {
            if (path.empty())
                return {};
            if (villain != 0)
                if (collection::Image held = collection::TrapPicture(toy, variant, villain, path); !held.Empty())
                    return FromImage(held);
            return FromImage(toy.category == collection::Category::CreationCrystal
                                 ? collection::CrystalPicture(toy, path)
                                 : collection::LoadPng(path));
        }

        // On the loader's thread: the newest cached art (independent of the viewed game, shared/collection/icons.h),
        // else the drawn icon.
        DecodedPicture IconPicture(
            const collection::Toy* toy, collection::VariantKind kind, bool large, uint16_t variant, uint16_t villain)
        {
            DecodedPicture picture =
                toy != nullptr ? FilePicture(*toy, collection::PortraitPath({}, *toy, variant, large), variant, villain)
                               : DecodedPicture{};
            if (picture.Empty())
            {
                constexpr int Size = 128;
                picture.rgba = figures::GenerateIcon(toy, kind, Size);
                picture.width = picture.height = Size;
                picture.generated = true;
            }
            return picture;
        }
    }

    uint16_t CollectionPage::HeldVillain(const collection::Figure& figure)
    {
        return figure.toy != nullptr && figure.toy->category == collection::Category::Trap ? figure.stats.villain : 0;
    }

    const FigureIconTexture& CollectionPage::Icon(
        const collection::Toy* toy, collection::VariantKind kind, bool large, uint16_t variant, uint16_t villain)
    {
        if (toy != nullptr && variant == 0)
            variant = collection::VariantFor(*toy, kind);
        std::string key = fmt::format("{}icon {} {} {} {} {}", FigurePictureKey, toy != nullptr ? toy->type : 0xFFFF,
            variant, int(kind), int(large), villain);
        auto [found, added] = m_icons->textures.try_emplace(key);
        if (added)
            m_app.m_pictures.Request(
                key, [toy, kind, large, variant, villain] { return IconPicture(toy, kind, large, variant, villain); });
        return found->second;
    }

    void CollectionPage::DrawIcon(ImDrawList* draw, const collection::Toy* toy, collection::VariantKind kind, ImVec2 at,
        float size, bool large, uint16_t variant, uint16_t villain)
    {
        const FigureIconTexture& icon = Icon(toy, kind, large, variant, villain);
        if (icon.texture == nullptr) // not read yet: the card's own fill
            return;
        const float alpha = FadeIn(icon.readyAt);
        draw->AddImage(Ref(icon.texture), at, ImVec2(at.x + size, at.y + size), ImVec2(0, 0), ImVec2(1, 1),
            ui::Col(0xFFFFFF, alpha));
        // A drawn icon gets the toy's initials, unless its family has a mark of its own (an item's potion...).
        if (!icon.generated || (toy != nullptr && collection::HasKindGlyph(toy->category)))
            return;
        std::string letters = Initials(toy != nullptr ? toy->name : "?");
        ImGui::PushFont(ui::Ctx().fonts.title, size * 0.3f); // the letters grow with the picture they are written over
        ImVec2 text = ImGui::CalcTextSize(letters.c_str());
        ImVec2 position(at.x + (size - text.x) / 2, at.y + (size - text.y) / 2);
        draw->AddText(ImVec2(position.x + ui::stroke::Hairline, position.y + 2 * ui::stroke::Hairline),
            ui::Col(ui::color::Scrim, 0.5f * alpha), letters.c_str());
        draw->AddText(position, ui::Col(ui::color::Ink, alpha), letters.c_str());
        ImGui::PopFont();
    }

    namespace
    {
        // A card's colour where its element icon goes until a disc gives the icon (the Toy Box's).
        uint32_t CornerRgb(const collection::Toy& toy)
        {
            if (toy.category == collection::Category::MagicItem || toy.category == collection::Category::Trophy)
                return 0x1E8F82;
            if (toy.category == collection::Category::Adventure)
                return 0xA8406F;
            return collection::ElementRgb(toy.element);
        }
    }

    void CollectionPage::DrawCornerIcons(ImDrawList* draw, const collection::Toy* toy, const collection::Toy* typed,
        int battleClass, ImVec2 at, float size)
    {
        if (toy == nullptr)
            return;
        // Read once per key on the loader's thread; ClearIcons (a disc's pictures were just read) asks again.
        auto icon = [&](const std::string& key, std::function<std::string()> find) -> const FigureIconTexture& {
            auto [found, added] = m_icons->textures.try_emplace(fmt::format("{}corner {}", FigurePictureKey, key));
            if (added)
                m_app.m_pictures.Request(found->first, [find = std::move(find)] {
                    const std::string path = find();
                    return path.empty() ? DecodedPicture{} : FromImage(collection::LoadPng(path));
                });
            return found->second;
        };
        const std::string game = m_model.gameId;
        const FigureIconTexture& element = icon(
            fmt::format("{} e{}", game, toy->type), [game, toy] { return collection::ElementIconPath(game, *toy); });
        if (element.texture != nullptr)
            DrawFitted(draw, element, at, size);
        else if (!element.pending)
            draw->AddCircleFilled(ImVec2(at.x + size / 2, at.y + size / 2), size / 3, ui::Col(CornerRgb(*toy)));
        const FigureIconTexture& type =
            battleClass != 0   ? icon(fmt::format("{} c{}", game, battleClass),
                                     [game, battleClass] { return collection::ClassIconPath(game, battleClass); })
            : typed != nullptr ? icon(fmt::format("{} t{}", game, typed->type),
                                     [game, typed] { return collection::TypeIconPath(game, *typed); })
                               : element;
        if (&type != &element && type.texture != nullptr)
            DrawFitted(draw, type, ImVec2(at.x, at.y + size + ui::space::XXS), size);
    }

    void CollectionPage::DrawFitted(ImDrawList* draw, const FigureIconTexture& picture, ImVec2 at, float box)
    {
        float scale = std::min(box / picture.width, box / picture.height), w = picture.width * scale,
              h = picture.height * scale;
        ImVec2 corner(at.x + (box - w) / 2, at.y + (box - h) / 2);
        draw->AddImage(Ref(picture.texture), corner, ImVec2(corner.x + w, corner.y + h), ImVec2(0, 0), ImVec2(1, 1),
            ui::Col(0xFFFFFF, FadeIn(picture.readyAt)));
    }

    void CollectionPage::DrawPicture(ImDrawList* draw, const collection::Toy* toy, collection::VariantKind kind,
        uint16_t variant, ImVec2 at, float box, uint16_t villain)
    {
        if (toy == nullptr)
        {
            DrawIcon(draw, toy, kind, at, box, true, variant);
            return;
        }
        std::string key = fmt::format("{}best {} {} {}", FigurePictureKey, toy->type, variant, villain);
        auto [found, added] = m_icons->textures.try_emplace(key);
        if (added)
            m_app.m_pictures.Request(key, [toy, variant, villain] {
                return FilePicture(*toy, collection::BestPicturePath({}, *toy, variant), variant, villain);
            }); // empty without a disc picture: the icon instead
        const FigureIconTexture& picture = found->second;
        if (picture.pending)
            return;
        if (picture.texture == nullptr || picture.width <= 0 || picture.height <= 0)
        {
            DrawIcon(draw, toy, kind, at, box, true, variant, villain);
            return;
        }
        DrawFitted(draw, picture, at, box);
    }

    bool CollectionPage::DrawSwapper(ImDrawList* draw, const collection::Toy* top, uint16_t topVariant,
        const collection::Toy* bottom, uint16_t bottomVariant, ImVec2 at, float box)
    {
        if (top == nullptr || bottom == nullptr)
            return false;
        std::string key =
            fmt::format("{}swapper {} {} {} {}", FigurePictureKey, top->type, topVariant, bottom->type, bottomVariant);
        auto [found, added] = m_icons->textures.try_emplace(key);
        if (added)
            m_app.m_pictures.Request(key, [top, topVariant, bottom, bottomVariant] {
                return FromImage(collection::SwapperPicture(*top, topVariant, *bottom, bottomVariant));
            }); // no picture is remembered too
        const FigureIconTexture& picture = found->second;
        if (picture.pending)
            return true; // nothing yet, and not the fallback either: it would show before the joined halves
        if (picture.texture == nullptr)
            return false;
        DrawFitted(draw, picture, at, box);
        return true;
    }

    bool CollectionPage::DrawSwapper(ImDrawList* draw, const collection::Card& card, ImVec2 at, float box)
    {
        return card.Swapper() &&
               DrawSwapper(draw, card.first->toy, card.first->variant, card.second->toy, card.second->variant, at, box);
    }

    // ---- Data -----------------------------------------------------------------------------------------------------

    std::string CollectionPage::Folder() const { return collection::Folder(); }

    void CollectionPage::Toast(const std::string& text, ui::ToastKind kind) { m_app.Toast(text, kind); }

    void CollectionPage::Open()
    {
        m_opened = true;
        collection::ClearStaleLocks();
        // One folder with the game: the old per-game folder is copied in (never moved), once per file.
        if (std::string legacy = collection::LegacyFolder(); !legacy.empty())
        {
            collection::Migration migration = collection::Migrate(legacy);
            if (migration.copied > 0)
                Toast(migration.copied == 1 ? Tf("{} figure copied", migration.copied)
                                            : Tf("{} figures copied", migration.copied),
                    ui::ToastKind::Success);
        }
        // The launcher's own favorites and portal list, from before the shared collection, move once.
        if (SettingGet(m_app.m_config, "collection", "moved") != "1")
        {
            bool moved = true;
            if (const collection::Game* game = m_model.Game())
            {
                std::vector<std::string> seats = MoveLauncherSettings(*game, collection::LegacyFolder(),
                    SplitList(SettingGet(m_app.m_config, "collection", "favourites")),
                    SplitList(SettingGet(m_app.m_config, "portal", "place")));
                if (!seats.empty())
                    moved = WriteSeats(*game, SessionFile(*game), seats);
            }
            if (moved)
            {
                m_app.Set("collection", "moved", "1");
                // The seats are the session file now: a [portal] place would override it at the game's start.
                m_app.Set("portal", "place", "");
            }
            else
                Toast(Tr("Couldn't write the seats for the next start"), ui::ToastKind::Error);
        }
        collection::PurgeTrash(30);
        // Only a game that can start with prepared seats has a portal here (one with a runtime: SSA, Giants).
        if (const collection::Game* game = m_model.Game(); game == nullptr || SessionFile(*game).empty())
            m_model.gameId = "ssa";
        LoadSort();
        Reload();
    }

    void CollectionPage::LoadSort() { m_filter.sort = collection::ParseSort(collection::GetSort("launcher")); }

    void CollectionPage::ChangeSort(collection::Sort sort)
    {
        if (sort == m_filter.sort)
            return;
        m_filter.sort = sort;
        m_shelfTop = true;
        if (!collection::SetSort("launcher", collection::SortId(sort)))
            Toast(Tr("Couldn't save the settings"), ui::ToastKind::Error);
    }

    void CollectionPage::Reload()
    {
        m_model.Load();
        m_generation++;
    }

    void CollectionPage::SelectPortalGame(const std::string& id)
    {
        m_model.gameId = id;
        m_choosing = -1;
        m_searchChoosing = false;
        Reload();
    }

    // The shelf: the filters, the search and the seat being chosen for, cut into the shelf's sections.
    void CollectionPage::Rebuild()
    {
        const bool realPortal = RealPortalInUse();
        std::string key = fmt::format("{}|{}|{}|{}|{}|{}|{}|{}|{}", m_generation, m_search, m_choosing,
            m_filter.element, m_filter.game + (m_allGames ? "*" : ""), m_filter.category, int(m_filter.sort),
            m_model.gameId, realPortal);
        if (key == m_listKey)
            return;
        m_listKey = key;
        ShelfFilter filter = m_filter;
        filter.text = m_search;
        filter.seat = m_choosing;
        filter.fitsGame = !m_allGames;
        m_sections = m_model.Shelf(filter, m_list, &m_hiddenStarters);
        m_starterBanner = m_model.ShowsStarterBanner(realPortal, m_choosing >= 0);
        m_starterNames.clear();
        for (const std::string& name : collection::StarterPackNames(m_model.gameId))
            m_starterNames += (m_starterNames.empty() ? "" : ", ") + name;
    }

    bool CollectionPage::RealPortalInUse() const
    {
        if (m_app.m_portal.Current().connected)
            return true;
        // The Toy Box window: the game says which seats hold a real figure.
        const collection::Game* game = m_model.Game();
        if (game == nullptr || !m_model.Live())
            return false;
        for (size_t seat = 0; seat < game->seats.size(); seat++)
            if (m_model.RealSeat(seat))
                return true;
        return false;
    }

    void CollectionPage::CreateStarterPack()
    {
        const collection::StarterPackResult result = collection::CreateStarterPack(m_model.gameId, m_model.figures);
        Reload();
        if (result.failed > 0)
            Toast(Tr("Couldn't create the starter pack"), ui::ToastKind::Error);
        else if (result.made == 0)
            Toast(Tr("Already in your collection"), ui::ToastKind::Info);
        else
            Toast(Tr("The starter pack is in your collection"), ui::ToastKind::Success);
    }

    void CollectionPage::DrawStarterBanner()
    {
        // Whose pack, what its box holds, and the button that creates it.
        constexpr float TextH = ui::type::Body + ui::space::XXS + ui::type::Caption;
        const float inner = std::max(TextH, float(ui::height::Normal));
        ui::BeginCard("##starterpack", ImVec2(0, inner + 2 * ui::space::S), { .padding = ui::space::S });
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float right = at.x + ImGui::GetContentRegionAvail().x;
        ui::ButtonOptions create;
        create.kind = ui::ButtonKind::Primary;
        const float createW = ui::ButtonWidth(Tr("Create starter pack"), create);
        const float room = right - createW - ui::space::M - at.x, textY = at.y + (inner - TextH) / 2;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        PutText(draw, ImVec2(at.x, textY), ui::TextStyle::Body, true, ui::color::Ink,
            Tf("No {} Skylanders yet", GameName(m_model.gameId.c_str(), GameNameForm::Short)), room);
        PutText(draw, ImVec2(at.x, textY + ui::type::Body + ui::space::XXS), ui::TextStyle::Caption, false,
            ui::color::InkSoft, m_starterNames, room);
        ImGui::SetCursorScreenPos(ImVec2(right - createW, at.y + (inner - ui::height::Normal) / 2));
        if (ui::Button(Tr("Create starter pack"), create))
            CreateStarterPack();
        ui::EndCard();
    }

    std::string WithoutHalf(std::string name)
    {
        for (const char* suffix : { " (top)", " (bottom)" })
        {
            std::string tail = suffix;
            if (name.size() > tail.size() && name.compare(name.size() - tail.size(), tail.size(), tail) == 0)
                return name.substr(0, name.size() - tail.size());
        }
        return name;
    }

    collection::Card CollectionPage::CardOf(const Figure& figure) const
    {
        if (!figure.partner.empty())
            for (const Figure& other : m_model.figures)
                if (other.file == figure.partner)
                    return figure.IsSwapBottom() ? collection::Card{ &other, &figure }
                                                 : collection::Card{ &figure, &other };
        return { &figure, nullptr };
    }

    // The bottom half of a swapper, small over the corner of the card's picture.
    void CollectionPage::DrawBottom(ImDrawList* draw, const Figure& bottom, ImVec2 at, float size)
    {
        const float edge = ui::stroke::Edge;
        draw->AddRectFilled(ImVec2(at.x - edge, at.y - edge), ImVec2(at.x + size + edge, at.y + size + edge),
            ui::Col(ui::color::Surface), ui::radius::Control);
        DrawIcon(draw, bottom.toy, bottom.Kind(), at, size, false, bottom.variant);
        draw->AddRect(ImVec2(at.x - edge, at.y - edge), ImVec2(at.x + size + edge, at.y + size + edge),
            ui::Col(ui::color::Edge), ui::radius::Control);
    }

    // The game's words: the player's name for it, else the toy's name with the nickname the game shows.
    std::string CollectionPage::Name(const Figure& figure) const
    {
        if (collection::Card card = CardOf(figure); card.Swapper())
            return collection::CardName(card);
        if (!figure.label.empty())
            return figure.label;
        if (figure.toy == nullptr)
            return Tf("Unknown toy {}", figure.type);
        std::string name = figure.DisplayName(), nickname = figures::NicknameUtf8(figure.stats);
        if (!nickname.empty() && Lower(nickname) != Lower(name))
            name += " (" + nickname + ")";
        return name;
    }

    // The line under a card's name, as in the Toy Box: the level for the portal's game ("Level 10 (15)"), or that a game
    // has it on its portal (the tooltip and the sheet say which).
    std::string CollectionPage::Detail(const Figure& figure) const
    {
        if (m_model.Incompatible(figure))
            return Tr(
                "Not for this game"); // the page names the game; a card is too narrow for "Not for Spyro's Adventure"
        collection::Card card = CardOf(figure);
        if (card.Busy())
            return Tr("Playing now");
        return card.Swapper() ? CardLevelLabel(card, m_model.gameId) : LevelLabel(figure, m_model.gameId);
    }

    std::string CollectionPage::SeatName(size_t seat) const
    {
        const collection::Game* game = m_model.Game();
        if (game == nullptr || seat >= game->seats.size())
            return {};
        const std::string& id = game->seats[seat].id;
        if (id == "p1" || id == "p2")
            return Tf("Player {}", id.substr(1));
        if (id == "item")
        {
            // Giants has one seat for a magic item or an adventure pack.
            const std::vector<Category>& accepts = game->seats[seat].accepts;
            return std::find(accepts.begin(), accepts.end(), Category::Adventure) != accepts.end()
                       ? Tr("Item or adventure")
                       : Tr("Magic item");
        }
        if (id == "adventure")
            return Tr("Adventure");
        if (id == "trap")
            return Tr("Trap");
        if (id.rfind("vehicle", 0) == 0)
            return Tf("Vehicle {}", id.substr(7));
        return id;
    }

    std::string CollectionPage::SeatTag(size_t seat) const
    {
        const collection::Game* game = m_model.Game();
        if (game == nullptr || seat >= game->seats.size())
            return {};
        return game->seats[seat].player > 0 ? SeatTagText(game->seats[seat]) : Tr("On");
    }

    std::string CollectionPage::SectionName(const Section& section) const
    {
        switch (section.kind)
        {
        case Section::Kind::Shelf: return Tr("Your figures");
        case Section::Kind::Element: return ElementName(collection::Element(section.value));
        case Section::Kind::Game:
            if (const collection::Game* game = collection::Catalogue::Get().FindGame(section.game))
                return game->name;
            return section.game;
        case Section::Kind::Category:
            return Category(section.value) == Category::Mini ? Tr("Minis") : CategoryLabel(Category(section.value));
        case Section::Kind::Series: return KindLabel(collection::VariantKind(section.value));
        case Section::Kind::Level: return Tf("Level {}", section.value);
        case Section::Kind::Letter: return std::string(1, char(section.value));
        case Section::Kind::Unknown: return Tr("Unknown toys");
        case Section::Kind::Foreign: return Tf("Not for {}", FullGameName(m_model.gameId));
        }
        return {};
    }

    std::string CollectionPage::LiveGame(const Figure& figure) const
    {
        collection::Card card = CardOf(figure);
        for (const Figure* half : card.Halves())
            if (half->lock.Busy())
                return ShortGameName(half->lock.game);
        return {};
    }

    bool CollectionPage::Writable(const Figure& figure) const { return !Busy(figure); }

    bool CollectionPage::SeatBusy(const Figure& figure) const
    {
        const collection::Card card = CardOf(figure);
        for (const Figure* half : card.Halves())
            if (half->lock.Busy() && !(m_model.Live() && half->lock.game == m_model.gameId))
                return true;
        return false;
    }

    // ---- Actions --------------------------------------------------------------------------------------------------

    void CollectionPage::OpenFigure(const std::string& path)
    {
        m_figure = path;
        m_changeKeep.clear();
        // A swapper's sheet is its top's.
        if (const Figure* figure = m_model.Find(path); figure != nullptr && figure->IsSwapBottom())
            if (collection::Card card = CardOf(*figure); card.Swapper())
                m_figure = card.first->path;
        m_overlay = Overlay::Figure;
        m_editing = false;
        m_confirm.clear();
        m_details = false;
        m_sheetShown.clear(); // it opens at the top, even right after another sheet
    }

    void CollectionPage::Choose(int seat)
    {
        const collection::Game* game = m_model.Game();
        if (seat >= 0 && (game == nullptr || size_t(seat) >= game->seats.size()))
            seat = -1;
        if (seat >= 0 && m_model.Live() && (!m_model.CanEditSeats() || m_model.RealSeat(size_t(seat))))
        {
            Toast(m_model.RealSeat(size_t(seat)) ? Tr("A real figure is there: lift it off the portal first")
                                                 : Tr("Waiting for the game..."));
            seat = -1;
        }
        if (seat >= 0 && !m_model.CanEditSeats())
        {
            Toast(SessionFile(*game).empty() ? Tf("Not available for {} yet", FullGameName(m_model.gameId))
                                             : Tr("The game is running: use its Toy Box"));
            seat = -1;
        }
        if (seat != m_choosing)
        {
            m_searchChoosing = false;
            m_generation++;
            m_shelfTop = seat >= 0; // choosing starts at the shelf's first row, never under a half-hidden one
        }
        m_choosing = seat;
    }

    std::string CollectionPage::Refusal(size_t seat, const Figure& figure, ui::ToastKind* kind) const
    {
        const collection::Game* game = m_model.Game();
        if (kind != nullptr)
            *kind = ui::ToastKind::Error;
        if (game == nullptr || seat >= game->seats.size())
            return Tr("Couldn't write the seats for the next start");
        if (!m_model.CanEditSeats())
        {
            if (kind != nullptr)
                *kind = ui::ToastKind::Info;
            return SessionFile(*game).empty() ? Tr("The game isn't installed yet")
                                              : Tr("The game is running: use its Toy Box");
        }
        if (m_model.Incompatible(figure))
            return Tf("{} is not for {}", Name(figure), FullGameName(m_model.gameId));
        if (!SeatAccepts(game->seats[seat], figure))
            return Tf("{} doesn't go on {}", Name(figure), SeatName(seat));
        if (SeatBusy(figure))
            return Tf("{} is in use by {}", Name(figure), LiveGame(figure));
        if (m_model.RealSeat(seat))
            return Tr("A real figure is there: lift it off the portal first");
        return {};
    }

    void CollectionPage::PutOn(size_t seat, const std::string& path)
    {
        const collection::Game* game = m_model.Game();
        const Figure* figure = m_model.Find(path);
        if (game == nullptr || figure == nullptr || seat >= game->seats.size())
            return;
        ui::ToastKind kind = ui::ToastKind::Error;
        if (const std::string refused = Refusal(seat, *figure, &kind); !refused.empty())
        {
            if (m_model.RealSeat(seat) && refused == Tr("A real figure is there: lift it off the portal first"))
                fprintf(stderr, "launcher: Toy Box window: seat %zu holds a real figure: not changed\n", seat);
            Toast(refused, kind);
        }
        else if (m_model.SetSeat(seat, path))
        {
            m_generation++;
            m_choosing = -1;
            if (!m_model.Live()) // the game answers with its seats (and says so if it cannot)
                Toast(fmt::format("{}: {}", SeatName(seat), Name(*figure)));
        }
        else
            Toast(Tr("Couldn't write the seats for the next start"), ui::ToastKind::Error);
    }

    void CollectionPage::MoveSeat(size_t from, size_t to)
    {
        const collection::Game* game = m_model.Game();
        if (game == nullptr || from == to || from >= m_model.seats.size() || to >= m_model.seats.size() ||
            to >= game->seats.size())
            return;
        const Figure* figure = m_model.Find(m_model.seats[from]);
        if (figure == nullptr)
            return;
        const Figure* other = m_model.seats[to].empty() ? nullptr : m_model.Find(m_model.seats[to]);
        if (other == nullptr || game->seats[from].player == 0 || game->seats[to].player == 0)
        {
            PutOn(to, figure->path); // empty: it moves; any other taken seat: replaced, as by a card
            return;
        }
        ui::ToastKind kind = ui::ToastKind::Error;
        std::string refused = Refusal(to, *figure, &kind);
        if (refused.empty())
            refused = Refusal(from, *other, &kind);
        if (!refused.empty())
        {
            Toast(refused, kind);
            return;
        }
        m_swapFrom = int(from);
        m_swapTo = int(to);
    }

    void CollectionPage::DrawSwapQuestion()
    {
        if (m_swapFrom < 0)
            return;
        const Figure* figure =
            size_t(m_swapFrom) < m_model.seats.size() ? m_model.Find(m_model.seats[m_swapFrom]) : nullptr;
        const Figure* other = size_t(m_swapTo) < m_model.seats.size() ? m_model.Find(m_model.seats[m_swapTo]) : nullptr;
        if (figure == nullptr || other == nullptr) // the seats changed meanwhile (the game, another window)
        {
            m_swapFrom = m_swapTo = -1;
            return;
        }
        bool open = true;
        const std::string body = Tf("{} goes on {}, and {} on {}.", Name(*figure), SeatName(size_t(m_swapTo)),
            Name(*other), SeatName(size_t(m_swapFrom)));
        const ui::ModalResult result =
            ui::ConfirmDialog("##swapplayers", Tr("Swap players?"), body.c_str(), Tr("Swap"), Tr("Cancel"), &open);
        if (result == ui::ModalResult::None && open)
            return;
        const size_t from = size_t(m_swapFrom), to = size_t(m_swapTo);
        m_swapFrom = m_swapTo = -1;
        if (result != ui::ModalResult::Confirm)
            return;
        if (!m_model.SwapSeats(from, to))
            Toast(Tr("Couldn't write the seats for the next start"), ui::ToastKind::Error);
        else
        {
            m_generation++;
            if (!m_model.Live())
                Toast(Tr("Players swapped"));
        }
    }

    bool CollectionPage::Drop(const std::string& path)
    {
        std::error_code error;
        if (fs::is_directory(datadir::FromText(path), error))
        {
            Import({ path });
            return true;
        }
        if (!collection::IsDumpName(datadir::Text(datadir::FromText(path).filename())))
            return false;
        Import({ path });
        return true;
    }

    void CollectionPage::Import(const std::vector<std::string>& paths)
    {
        if (!m_opened)
            Open();
        collection::ImportReport report = collection::Import(paths);
        Reload();
        if (report.added > 0)
            m_allGames = true; // an import can be another game's figure: shown, with the filter's chip
        std::string text = Tf("Imported {}", report.added);
        if (report.duplicates > 0)
            text += Tf(" · {} already there", report.duplicates);
        if (report.invalid > 0)
            text += Tf(" · {} not a figure", report.invalid);
        if (report.sameToy > 0)
            text += report.sameToy == 1 ? Tf(" · {} is a toy you already have", report.sameToy)
                                        : Tf(" · {} are toys you already have", report.sameToy);
        if (!report.unknown.empty())
            text += report.unknown.size() == 1 ? Tf(" · {} unknown toy (Manage)", report.unknown.size())
                                               : Tf(" · {} unknown toys (Manage)", report.unknown.size());
        Toast(text);
    }

    void CollectionPage::CloseOverlay()
    {
        m_overlay = Overlay::None;
        m_editing = false;
        m_confirm.clear();
        m_addSeat = -1;
        ImGui::CloseCurrentPopup();
    }

    void CollectionPage::OpenAdd(int seat)
    {
        m_overlay = Overlay::Add;
        m_addToy = 0xFFFF;
        m_created.clear();
        m_addGame = m_model.gameId;
        m_addSeat = seat;
        m_addCategory = -1;
        m_addFits = seat >= 0;
    }

    // Empties a seat: the figure leaves the portal of the next start (and stays in the collection).
    void CollectionPage::TakeOff(size_t seat)
    {
        const collection::Game* game = m_model.Game();
        if (game == nullptr || seat >= game->seats.size() || m_model.seats[seat].empty())
            return;
        std::string what = m_model.seats[seat];
        const Figure* figure = m_model.Find(what);
        if (!m_model.CanEditSeats())
            Toast(Tr("The game is running: use its Toy Box"));
        else if (m_model.Live())
        {
            if (m_model.SetSeat(seat, ""))
                m_generation++;
        }
        else if (m_model.SetSeat(seat, ""))
        {
            m_generation++;
            Toast(
                Tf("{}: {} taken off", SeatName(seat), figure != nullptr ? Name(*figure) : std::string(Tr("figure"))));
        }
        else
            Toast(Tr("Couldn't write the seats for the next start"), ui::ToastKind::Error);
    }

    void CollectionPage::TestSetSeats(const std::vector<std::string>& seats)
    {
        for (size_t i = 0; i < seats.size(); i++)
            m_model.SetSeat(i, seats[i]);
        m_generation++;
    }

    // ---- The page -------------------------------------------------------------------------------------------------

    namespace
    {
        // The page's own parts, sized from the tokens.
        constexpr float PageMax = 1180; // the widest the page's column gets
        constexpr float SeatPicture = ui::height::Normal;
        constexpr float SeatPad = ui::space::M; // inside a seat, round its picture and its lines
        constexpr float SeatHeight = SeatPicture + 2 * SeatPad;
        constexpr float SeatGap = ui::space::M; // between the seats
        constexpr float ShelfCell = 2 * ui::height::Main; // the narrowest a figure's card gets
        constexpr float ShelfGap = ui::space::XS;
        constexpr int FilterCount = 3; // Element, Works in, Type
        constexpr float FilterPanel = ui::height::SettingControl + 2 * ui::height::Main;
        constexpr float RefusedAlpha = 0.35f; // a seat that would refuse the figure being dragged
        constexpr float DraggedAlpha = 0.75f; // the dragged figure's picture under the pointer

        // What was drawn into `draw` since vertex `from`, at `alpha` of its own opacity.
        void FadeVertices(ImDrawList* draw, int from, float alpha)
        {
            for (int v = from; v < draw->VtxBuffer.Size; v++)
            {
                ImU32& col = draw->VtxBuffer[v].col;
                const ImU32 a = ImU32(float((col >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
                col = (col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
            }
        }

        // A card's fill: the surface (raised under the pointer or the focus) at the elevation's alpha, faded by `fade`.
        ImU32 CardFill(bool raised, float fade = 1)
        {
            return ui::Col(
                raised ? ui::color::Raised : ui::color::Surface, fade * float(ui::elevation::Card.fillAlpha) / 255.0f);
        }

    }

    void PutText(ImDrawList* draw, ImVec2 at, ui::TextStyle style, bool bold, unsigned color, const std::string& text,
        float room)
    {
        ui::PushText(style, bold);
        ui::DrawText(draw, at, ui::Col(color), ui::Ellipsize(text.c_str(), room).c_str());
        ui::PopText();
    }

    void CollectionPage::Draw()
    {
        if (!m_opened)
            Open();
        if (m_model.PollInBackground(SDL_GetTicks()))
            m_generation++; // a game wrote a dump, took or released a figure, or quit
        if (m_choosing >= 0 && !m_model.CanEditSeats())
            m_choosing = -1; // a game started meanwhile
        Rebuild();

        float available = ImGui::GetContentRegionAvail().x;
        float width = std::min(available, PageMax);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) / 2);
        m_app.BeginPageColumn("##collectioncolumn", width);
        DrawHeader(width);
        DrawSeats(width);
        if (m_starterBanner)
            DrawStarterBanner();
        DrawBar();
        // The shelf has room round its cards (their focus rings, the choose banner's border).
        const bool pad = ui::Ctx().glyphs != ui::Glyphs::Keyboard;
        const float legend = pad ? ui::height::Normal + ImGui::GetStyle().ItemSpacing.y : 0;
        ui::BeginRoomyChild("##shelf", ImVec2(0, -legend), ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
        ImGui::SetScrollX(0.0f);
        if (m_shelfTop)
        {
            ImGui::SetScrollY(0.0f);
            m_shelfTop = false;
        }
        DrawList();
        if (m_testScrollBottom)
            ImGui::SetScrollY(ImGui::GetScrollMaxY());
        ui::EndRoomyChild();
        // A figure dragged off a seat and dropped on the shelf leaves the seat.
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SKYSEAT"))
                TakeOff(*static_cast<const size_t*>(payload->Data));
            ImGui::EndDragDropTarget();
        }
        HandleShortcuts();
        if (pad) // the legend's room in the column
            ImGui::Dummy(ImVec2(width, ui::height::Normal));
        ImGui::EndChild();
        if (pad)
            m_app.DrawFootLegend(); // with the page's pad actions (HandleShortcuts)

        DrawSwapQuestion();
        DrawOverlay();
    }

    void CollectionPage::DrawHeader(float width)
    {
        const ImVec2 top = ImGui::GetCursorScreenPos();
        ui::Text(ui::TextStyle::Display, m_model.Live() ? Tr("Toy Box") : Tr("Collection"), ui::color::Ink,
            -1); // the window is the Toy Box
        const ImVec2 title = ImGui::GetItemRectSize();
        float x = ImGui::GetItemRectMax().x + ui::space::M;
        const int shown = int(m_model.figures.size()) - m_hiddenStarters;
        const size_t favourites =
            std::count_if(m_model.figures.begin(), m_model.figures.end(), [](const Figure& f) { return f.favourite; });
        std::string summary = FigureCountText(shown);
        if (favourites > 0)
            summary += favourites == 1 ? Tf(" · {} favorite", favourites) : Tf(" · {} favorites", favourites);
        ui::Tooltip(summary.c_str());

        // Add a figure, and the rest of the collection's work under "...".
        ui::ButtonOptions add;
        add.kind = ui::ButtonKind::Primary;
        add.icon = ui::Icon::Plus;
        const float addWidth = ui::ButtonWidth(Tr("Add a figure"), add);
        // On the title's line: the buttons' middle on its capitals' middle.
        const float middle = top.y + ui::CapMiddle(ui::TextStyle::Display);
        const float buttonsY = std::round(middle - ui::height::Normal / 2);
        const float row = std::max(title.y, buttonsY + ui::height::Normal - top.y);
        const float addX = top.x + width - addWidth - ui::space::S - ui::height::Normal;
        const float end = addX - ui::space::M;

        // The game the seats are for, then what still has room before the buttons.
        const bool running = !m_model.Live() && m_model.RunningGame() == m_model.gameId;
        const bool hasWindow = running && m_app.m_session.Running() == m_model.gameId;
        if (m_model.Live())
            DrawLiveLine(ImVec2(x, buttonsY), end - x);
        else
        {
            ui::SelectOptions games;
            for (const collection::Game& g : collection::Catalogue::Get().Games())
                if (!SessionFile(g).empty())
                    games.width = std::max(games.width, ui::SelectWidth(ShortGameName(g.id).c_str()));
            games.width = std::max(2 * ui::height::Normal, std::min(games.width, end - x));
            ImGui::SetCursorScreenPos(ImVec2(x, buttonsY));
            if (ui::BeginSelect("##portalgame", ShortGameName(m_model.gameId).c_str(), games))
            {
                for (const collection::Game& g : collection::Catalogue::Get().Games())
                    if (!SessionFile(g).empty() &&
                        ui::SelectOption(ShortGameName(g.id).c_str(), g.id == m_model.gameId))
                        SelectPortalGame(g.id);
                ui::EndSelect();
            }
            else
                ui::Tooltip(Tr("The figures on the portal when the game starts"));
            x += games.width + ui::space::M;
            auto pillFits = [&](const std::string& text) {
                return x + ui::PillWidth(text.c_str()) + ui::space::L <= end;
            };
            const float pillY = std::round(middle - ui::PillHeight / 2);
            if (hasWindow)
            {
                // Its Toy Box in a window of its own, beside the game (toybox_window.cpp).
                const ui::ButtonOptions window{ .kind = ui::ButtonKind::Secondary,
                    .size = ui::Size::Small,
                    .tooltip = Tr("The Toy Box in its own window, to keep beside the game") };
                const float w = ui::ButtonWidth(Tr("Toy Box window"), window);
                if (x + w <= end)
                {
                    ImGui::SetCursorScreenPos(ImVec2(x, std::round(middle - ui::height::Small / 2)));
                    if (ui::Button(Tr("Toy Box window"), window))
                        m_app.OpenToyboxWindow();
                    x += w + ui::space::S;
                }
            }
            if (running)
            {
                const std::string text = Tf("{} is running: use its Toy Box", ShortGameName(m_model.gameId));
                if (pillFits(text))
                {
                    ImGui::SetCursorScreenPos(ImVec2(x, pillY));
                    ui::StatusPill(ui::Status::Info, text.c_str());
                }
            }
            else if (const std::string portal = m_app.m_portal.Current().Line(); !portal.empty() && pillFits(portal))
            {
                ImGui::SetCursorScreenPos(ImVec2(x, pillY));
                m_app.DrawPortalStatus();
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(addX, buttonsY));
        if (ui::Button(Tr("Add a figure"), add))
            OpenAdd();
        m_app.TakeFocus(FocusTo::Page);
        ImGui::SameLine(0, ui::space::S);
        if (ui::IconButton("##manage", ui::Icon::More, Tr("Manage"), { ui::ButtonKind::Secondary }) || m_testMenu == 1)
        {
            ui::OpenMenu("##collectionmenu", { .alignRight = true });
            m_testMenu = 0;
        }
        if (ui::BeginMenu("##collectionmenu"))
        {
            auto entry = [&](const char* label, int tab, ui::Icon icon = ui::Icon::None) {
                if (ui::MenuItem(label, { icon }))
                {
                    m_overlay = Overlay::Manage;
                    m_manageTab = tab;
                }
            };
            entry(Tr("Trash"), 0, ui::Icon::Trash);
            entry(Tr("Backups"), 1);
            entry(Tr("Import and export"), 2, ui::Icon::Download);
            entry(Tr("Unknown toys"), 3);
            entry(Tr("Tidy the starters"), 4);
            ui::MenuSeparator();
            if (ui::MenuItem(Tr("Show the collection folder"), { ui::Icon::Folder }))
            {
                std::string folder = collection::Folder();
                OpenFolder(std::filesystem::path(std::u8string(folder.begin(), folder.end())));
            }
            // Here whatever the header's width.
            if (m_model.Live())
            {
                const bool withGame = m_app.ToyboxWithGame();
                if (ui::MenuItem(Tr("Open with the game"), { .checked = withGame }))
                    m_app.SetToyboxWithGame(!withGame);
            }
            else if (hasWindow && ui::MenuItem(Tr("Toy Box window")))
                m_app.OpenToyboxWindow();
            ui::EndMenu();
        }
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + row + ui::space::M));
    }

    void CollectionPage::DrawSeatMenu()
    {
        if (!ui::BeginMenu("##seatmenu"))
            return;
        const size_t seat = size_t(m_seatMenu);
        const Figure* figure =
            seat < m_model.seats.size() && !m_model.seats[seat].empty() ? m_model.Find(m_model.seats[seat]) : nullptr;
        if (seat >= m_model.seats.size() || m_model.seats[seat].empty())
            ImGui::CloseCurrentPopup();
        else
        {
            if (ui::MenuItem(Tr("Take it off"), { ui::Icon::Cross, false, false, !m_model.CanEditSeats() }))
                TakeOff(seat);
            if (ui::MenuItem(Tr("Choose another"), { ui::Icon::None, false, false, !m_model.CanEditSeats() }))
                Choose(int(seat));
            if (ui::MenuItem(Tr("Open its sheet"), { ui::Icon::None, false, false, figure == nullptr }))
                OpenFigure(figure->path);
        }
        ui::EndMenu();
    }

    // The portal at the next start: the chosen game's seats in a row, what each holds, and how to change it.
    void CollectionPage::TestHeroPortal(bool installing, bool running)
    {
        m_heroFixture = m_heroLoaded = true;
        m_heroRealPortal = running && LauncherEnv("SKY_LAUNCHER_TEST_HERO_REAL_PORTAL") != nullptr;
        m_model.figures.clear();
        m_model.seats.assign(3, "");
        for (int i = 0; i < (installing ? 1 : 2); i++)
        {
            Figure figure;
            figure.type = i == 0 ? 9 : 19;
            figure.toy = collection::Catalogue::Get().Find(figure.type);
            figure.variantInfo.name = figure.toy != nullptr ? figure.toy->name : "?";
            figure.path = i == 0 ? "hero-eruptor" : "hero-trigger-happy";
            figure.level = i == 0 ? (running ? 11 : 10) : 2;
            m_model.seats[size_t(i)] = figure.path;
            m_model.figures.push_back(figure);
        }
    }

    void CollectionPage::DrawHeroPortal(ImVec2 at, ImVec2 size, bool live)
    {
        // Read Collection's seats without opening its migration or editing flows.
        if (m_model.gameId != m_app.m_s->game.id)
        {
            SelectPortalGame(m_app.m_s->game.id);
            m_heroLoaded = true;
        }
        if (!m_heroLoaded)
        {
            m_model.Load();
            m_heroLoaded = true;
        }
        if (!m_heroFixture)
            m_model.PollInBackground(SDL_GetTicks());
        if (m_model.Game() == nullptr)
            return; // a game the catalogue doesn't list yet has no seats to show
        ImGui::SetCursorScreenPos(at);
        ui::BeginCard(
            "##hero-portal", size, { .panel = true, .padding = ui::space::M, .edge = live ? ui::color::Success : 0 });
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui::space::S, ui::space::XS));
        // While the game runs, the button opens the Toy Box window beside it (toybox_window.cpp); else the Collection page.
        const ui::ButtonOptions open{ .kind = ui::ButtonKind::Secondary,
            .size = ui::Size::Small,
            .tooltip = live ? Tr("The Toy Box in its own window, to keep beside the game") : Tr("Open the Toy Box") };
        const float titleW = ui::TextSize(ui::TextStyle::Body, Tr("On the portal"), nullptr, true).x +
                             (live ? ui::BadgeSize(Tr("Live")).x + ui::space::S : 0);
        const char* wide = live ? Tr("Toy Box window") : Tr("Open the Toy Box");
        const char* openLabel =
            titleW + ui::space::S + ui::ButtonWidth(wide, open) <= size.x - 2 * ui::space::M ? wide : Tr("Toy Box");
        const float linkW = ui::ButtonWidth(openLabel, open);
        const float headerY = at.y + ui::space::M;
        ImGui::SetCursorScreenPos(ImVec2(at.x + ui::space::M,
            std::round(headerY + ui::height::Small / 2 - ui::CapMiddle(ui::TextStyle::Body, true))));
        ui::Text(ui::TextStyle::Body, Tr("On the portal"), ui::color::Dim, -1, true);
        const float labelRight = ImGui::GetItemRectMax().x;
        if (live) // on the title's line, so the seats do not move when the game starts
        {
            const ImVec2 badge = ui::BadgeSize(Tr("Live"));
            ImGui::SetCursorScreenPos(
                ImVec2(labelRight + ui::space::S, std::round(headerY + (ui::height::Small - badge.y) / 2)));
            ui::Badge(Tr("Live"), ui::color::Success);
        }
        ImGui::SetCursorScreenPos(ImVec2(at.x + size.x - ui::space::M - linkW, headerY));
        if (ui::Button(openLabel, open))
        {
            if (live)
                m_app.OpenToyboxWindow();
            else
                m_app.GoTo(Page::Collection);
        }
        const float rowsY = headerY + ui::height::Small + ui::space::XS;
        const collection::Game* game = m_model.Game();
        // Every seat the game has, as in its Collection: Trap Team's trap, SuperChargers' vehicles, the adventure pack.
        const size_t count = game != nullptr ? game->seats.size() : 0;
        const float portalY = at.y + size.y - ui::space::M - ui::height::Small; // the portal's line, under the seats
        const float rowH = count == 0 ? 0 : (portalY - ui::space::XS - rowsY - (count - 1) * ui::space::XS) / count;
        // A real portal plugged in: the seats of the session file are not on the game's portal. Player 1 as the game
        // plays it; the other seats are only known to the Toy Box window, over its link.
        bool realPortal = false;
        std::optional<discordpresence::Toy> gameToy;
        if (m_heroFixture)
        {
            realPortal = live && m_heroRealPortal;
            gameToy = discordpresence::Toy{ 19, 0, 12 };
        }
        else if (live && m_app.m_portalGame == m_model.gameId)
        {
            realPortal = m_app.m_portalWatch.RealPortal();
            gameToy = m_app.m_portalWatch.PlayerOne();
        }
        Figure played;
        if (realPortal && gameToy)
        {
            played.type = gameToy->type;
            played.variant = gameToy->variant;
            played.toy = collection::Catalogue::Get().Find(played.type);
            played.variantInfo = collection::Catalogue::Get().Describe(played.toy, played.variant);
            played.level = gameToy->level ? int(*gameToy->level) : 0;
        }
        size_t rowIndex = 0;
        if (game != nullptr)
            for (size_t i = 0; i < game->seats.size(); i++)
            {
                if (realPortal && game->seats[i].id != "p1")
                    continue;
                const float rowY = std::floor(rowsY + rowIndex * (rowH + ui::space::XS));
                ImGui::SetCursorScreenPos(ImVec2(at.x + ui::space::M, rowY));
                rowIndex++;
                const Figure* figure = realPortal                 ? (gameToy ? &played : nullptr)
                                       : i < m_model.seats.size() ? m_model.Find(m_model.seats[i])
                                                                  : nullptr;
                const std::string seat = SeatName(i);
                const std::string name = figure == nullptr ? Tf("{} · empty", seat)
                                         : realPortal      ? collection::NameIn(*figure, m_model.gameId)
                                                           : Name(*figure);
                std::string detail;
                if (realPortal && figure != nullptr)
                    detail = figure->level > 0 ? Tf("{} · {}", seat, Tf("Level {}", figure->level)) : seat;
                else if (figure != nullptr)
                {
                    const collection::Card card = CardOf(*figure);
                    const std::string level = m_heroFixture    ? Tf("Level {}", figure->level)
                                              : card.Swapper() ? CardLevelLabel(card, m_model.gameId)
                                                               : LevelLabel(*figure, m_model.gameId);
                    detail = level.empty() ? seat : Tf("{} · {}", seat, level);
                }
                ImGui::PushID(int(i));
                ui::ListRowOptions row;
                row.subtitle = figure != nullptr ? detail.c_str() : nullptr;
                row.titleStyle = figure != nullptr ? ui::TextStyle::Label : ui::TextStyle::Body;
                row.subtitleStyle = ui::TextStyle::Body;
                row.minHeight = std::floor(rowsY + rowIndex * rowH + (rowIndex - 1) * ui::space::XS) - rowY;
                if (row.minHeight <
                    ui::height::Large) // many seats (SuperChargers' vehicles): the rows fit the card, the lines smaller
                {
                    row.exactHeight = row.minHeight;
                    row.titleStyle = ui::TextStyle::Body;
                    row.subtitleStyle = ui::TextStyle::Caption;
                }
                row.dim = figure == nullptr;
                row.imageWidth = ui::height::Normal;
                row.image = [&](ImDrawList* draw, ImVec2 picture, ImVec2 box) {
                    const float side = std::min(box.x, box.y);
                    picture.y += (box.y - side) / 2;
                    if (figure != nullptr)
                    {
                        if (realPortal || !DrawSwapper(draw, CardOf(*figure), picture, side))
                            DrawPicture(draw, figure->toy, figure->Kind(), figure->variant, picture, side,
                                HeldVillain(*figure));
                    }
                    else
                        ui::DrawEmptySeat(draw, picture, side);
                };
                if (ui::ListRow((name + "##hero-seat").c_str(), row))
                {
                    if (!m_opened)
                        Open();
                    Choose(live ? -1 : int(i));
                    m_app.GoTo(Page::Collection);
                }
                ImGui::PopID();
            }
        if (realPortal && rowIndex < count)
        {
            const float rowY = std::floor(rowsY + rowIndex * (rowH + ui::space::XS));
            ImGui::SetCursorScreenPos(ImVec2(at.x + ui::space::M,
                std::round(rowY + std::min(rowH, float(ui::height::Large)) / 2 - ui::CapMiddle(ui::TextStyle::Body))));
            ui::Text(ui::TextStyle::Body, Tr("The other seats: in the Toy Box window"), ui::color::Dim,
                size.x - 2 * ui::space::M);
        }
        // The portal the game has: a link to its settings, plain words while the game runs (the seats do not move).
        const launcher_portal::Status portal = m_app.m_testPortal ? *m_app.m_testPortal : m_app.m_portal.Current();
        const char* portalName = (live ? realPortal : portal.connected) ? Tr("Portal") : Tr("Virtual portal");
        if (live)
        {
            ImGui::SetCursorScreenPos(ImVec2(at.x + ui::space::M,
                std::round(portalY + ui::height::Small / 2 - ui::CapMiddle(ui::TextStyle::Caption))));
            ui::Text(ui::TextStyle::Caption, portalName, ui::color::Dim, -1);
        }
        else
        {
            const std::string tip =
                portal.Line() + "\n" + Tr("Without a real portal, the game uses its virtual one (the Toy Box).");
            ImGui::SetCursorScreenPos(ImVec2(at.x + ui::space::M, portalY));
            if (ui::Button((std::string(portalName) + "##hero-portal-link").c_str(),
                    { .kind = ui::ButtonKind::Ghost,
                        .size = ui::Size::Small,
                        .icon = portal.connected ? ui::IconOf(ui::Status::Success) : ui::Icon::None,
                        .trailing = ui::Icon::ChevronRight,
                        .overArt = true,
                        .tooltip = tip.c_str() }))
            {
                m_app.m_tab = SettingsTab::Portal;
                m_app.GoTo(Page::Settings);
            }
        }
        if (m_app.m_testInstall)
        {
            const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
            m_app.m_drawn["install-portal-rect"] = fmt::format("{},{},{},{}", min.x, min.y, max.x, max.y);
        }
        ImGui::PopStyleVar();
        ui::EndCard();
    }

    void CollectionPage::DrawLiveLine(ImVec2 at, float room)
    {
        using State = toyboxlinkclient::Client::State;
        const State state = m_model.live->Current();
        const float middle = at.y + ui::height::Normal / 2;
        const bool connected = state == State::Connected;
        const char* text = connected                       ? Tr("Live")
                           : state == State::OtherVersion  ? (m_model.live->GameVersion() > toyboxlink::Version
                                                                     ? Tr("The game is newer: update the launcher")
                                                                     : Tr("The game is older: update it"))
                           : state == State::Busy          ? Tr("Another Toy Box window has this game")
                           : m_app.ToyboxGameWithoutLink() ? Tr("The game is older: update it")
                                                           : Tr("Waiting for the game...");
        const bool warning =
            !connected && state != State::Busy && (state == State::OtherVersion || m_app.ToyboxGameWithoutLink());
        // The state before the game's name, which the seats and the game's own window say too.
        const float stateWidth = connected ? ui::BadgeSize(text).x : ui::PillWidth(text) + ui::space::L;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        if (stateWidth > room)
        {
            PutText(draw, ImVec2(at.x, middle - ui::type::Caption / 2), ui::TextStyle::Caption, false,
                warning ? ui::color::Warning : ui::color::InkSoft, text, room);
            return;
        }
        ImGui::SetCursorScreenPos(
            ImVec2(at.x, std::round(middle - (connected ? ui::BadgeSize(text).y : ui::PillHeight) / 2)));
        if (connected)
            ui::Badge(text, ui::color::Success);
        else
            ui::StatusPill(warning ? ui::Status::Warning : ui::Status::Info, text);
        const std::string game = Tf("On the portal of {}", ShortGameName(m_model.gameId));
        const float left = stateWidth + ui::space::M;
        ui::PushText(ui::TextStyle::Body, true);
        const bool named = left + ImGui::CalcTextSize(game.c_str()).x <= room;
        ui::PopText();
        if (named)
            PutText(draw, ImVec2(at.x + left, middle - ui::type::Body / 2), ui::TextStyle::Body, true,
                ui::color::InkSoft, game.c_str(), room - left);
    }

    void CollectionPage::DrawRealSeat(ImDrawList* draw, size_t seat, ImVec2 at, ImVec2 end)
    {
        // What the game read from a toy on the real portal: its picture and name once the game read its type, else "A real
        // figure". Read only: only lifting it off the portal changes it.
        const toyboxlink::Seat& real = m_model.liveSeats[seat];
        const collection::Toy* toy = real.known ? collection::Catalogue::Get().Find(real.type) : nullptr;
        const ImVec2 picture(at.x + SeatPad, at.y + SeatPad);
        const float tx = picture.x + SeatPicture + ui::space::S, room = end.x - tx - SeatPad;
        const float top =
            at.y + std::round((SeatHeight - (2 * ui::type::Caption + ui::type::Body + 2 * ui::space::XXS)) / 2);
        const float second = top + ui::type::Caption + ui::space::XXS, third = second + ui::type::Body + ui::space::XXS;
        std::string name = Tr("A real figure");
        if (toy != nullptr)
        {
            const collection::VariantInfo info = collection::Catalogue::Get().Describe(toy, real.variant);
            DrawIcon(draw, toy, info.kind, picture, SeatPicture, false, real.variant);
            name = info.name.empty() ? toy->name : info.name;
        }
        else
        {
            const ImVec2 box(picture.x + SeatPicture, picture.y + SeatPicture);
            draw->AddRectFilled(picture, box, ui::Col(ui::color::Inset), ui::radius::Control);
            ui::DrawIcon(draw, ui::Icon::Circle, ImVec2(picture.x + SeatPicture / 2, picture.y + SeatPicture / 2),
                ui::icon::Normal, ui::Col(ui::color::Dim));
        }
        PutText(draw, ImVec2(tx, second), ui::TextStyle::Body, true, ui::color::Ink, name, room);
        PutText(draw, ImVec2(tx, third), ui::TextStyle::Caption, false, ui::color::Success,
            Tr("Real figure · on the portal"), room);
    }

    void CollectionPage::DrawSeatPicture(ImDrawList* draw, const Figure& figure, ImVec2 at, float size)
    {
        if (collection::Card card = CardOf(figure); !DrawSwapper(draw, card, at, size))
        {
            DrawIcon(draw, figure.toy, figure.Kind(), at, size, false, figure.variant, HeldVillain(figure));
            if (card.Swapper())
                DrawBottom(draw, *card.second, ImVec2(at.x + size * 0.58f, at.y + size * 0.58f), size * 0.42f);
        }
    }

    void CollectionPage::DrawDragged(const Figure& figure)
    {
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        const ImVec2 pointer = ImGui::GetMousePos();
        const int first = draw->VtxBuffer.Size;
        DrawSeatPicture(draw, figure, ImVec2(pointer.x - SeatPicture / 2, pointer.y - SeatPicture / 2), SeatPicture);
        FadeVertices(draw, first, DraggedAlpha);
    }

    void CollectionPage::DrawSeats(float width)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const bool editable = m_model.CanEditSeats();
        // Choosing another game in the header reloads the seats at once: the game read before may have more of them (Trap
        // Team and SuperChargers have 5, Giants 3), so read it here and never go past the seats loaded (issue #45).
        const collection::Game* game = m_model.Game();
        if (game == nullptr)
            return;

        const size_t count = std::min(game->seats.size(), m_model.seats.size());
        if (count == 0)
            return;
        // One row, or two when a seat's label would be cut (seven seats in Imaginators): the players' seats first.
        std::vector<bool> playerSeat;
        float widest = 0;
        ui::PushText(ui::TextStyle::Caption, false);
        for (size_t i = 0; i < count; i++)
        {
            playerSeat.push_back(game->seats[i].player != 0);
            widest = std::max(widest, ImGui::CalcTextSize(SeatName(i).c_str()).x);
        }
        ui::PopText();
        // The label's room: the seat less its padding, picture and gap, and the X of an occupied seat.
        auto labelRoom = [](float w) { return w - 2 * SeatPad - SeatPicture - ui::space::S - ui::space::L; };
        const std::vector<std::vector<size_t>> rows = SeatRows(playerSeat, width, SeatGap, widest, labelRoom);
        const ImVec2 row = ImGui::GetCursorScreenPos();
        std::vector<ImVec2> seatAt(count), seatEnd(count);
        for (size_t r = 0; r < rows.size(); r++)
        {
            const float w = SeatWidth(width, SeatGap, rows[r].size());
            const float y = row.y + float(r) * (SeatHeight + SeatGap);
            for (size_t k = 0; k < rows[r].size(); k++)
            {
                const float x = row.x + float(k) * (w + SeatGap);
                seatAt[rows[r][k]] = ImVec2(x, y);
                seatEnd[rows[r][k]] = ImVec2(k + 1 == rows[r].size() ? row.x + width : x + w, y + SeatHeight);
            }
        }
        m_navSeat = m_hoverSeat = -1;
        m_crossAt.assign(count, ImVec2(-1, -1));
        // The figure being dragged (a card, or a seat's), for the seats that would refuse it and the one it is over.
        const Figure* dragged = nullptr;
        int draggedFrom = -1;
        if (const ImGuiPayload* payload = ImGui::GetDragDropPayload())
        {
            if (payload->IsDataType("SKYFIG"))
                dragged = m_model.Find(static_cast<const char*>(payload->Data));
            else if (payload->IsDataType("SKYSEAT"))
            {
                draggedFrom = int(*static_cast<const size_t*>(payload->Data));
                if (size_t(draggedFrom) < count && !m_model.seats[draggedFrom].empty())
                    dragged = m_model.Find(m_model.seats[draggedFrom]);
            }
        }
        for (size_t i = 0; i < count; i++)
        {
            const ImVec2 at = seatAt[i], end = seatEnd[i];
            ImGui::SetCursorScreenPos(at);
            ImGui::PushID(int(i));
            // The X is submitted over the seat: without this the seat keeps the hover and the X never sees the pointer.
            ImGui::SetNextItemAllowOverlap();
            if (ui::Hit("##seat", ImVec2(end.x - at.x, SeatHeight)))
                Choose(m_choosing == int(i) ? -1 : int(i));
            if (m_focusSeat == int(i))
            {
                ui::FocusLastItem(true);
                m_focusSeat = -1;
            }
            const ui::State state = ui::ItemState();
            const bool hovered = state.hovered, nav = ImGui::IsItemFocused();
            ui::FocusRingIf(state, ui::radius::Card);
            const bool refuses = dragged != nullptr && draggedFrom != int(i) && !Refusal(i, *dragged).empty();
            // Dropping a card on a seat puts the figure there; a seat's figure moves (MoveSeat).
            bool target = false;
            if (ImGui::BeginDragDropTarget())
            {
                constexpr ImGuiDragDropFlags early =
                    ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SKYFIG", early))
                {
                    target = true;
                    if (payload->IsDelivery())
                        PutOn(i, std::string(static_cast<const char*>(payload->Data)));
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SKYSEAT", early))
                {
                    target = *static_cast<const size_t*>(payload->Data) != i;
                    if (payload->IsDelivery())
                        MoveSeat(*static_cast<const size_t*>(payload->Data), i);
                }
                ImGui::EndDragDropTarget();
            }
            const Figure* figure = m_model.seats[i].empty() ? nullptr : m_model.Find(m_model.seats[i]);
            const bool occupied = !m_model.seats[i].empty();
            if (nav)
                m_navSeat = int(i);
            if (hovered)
                m_hoverSeat = int(i);
            // Dragging a figure off its seat, onto the shelf, takes it off; onto another seat, moves it.
            if (occupied && editable && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
            {
                size_t index = i;
                ImGui::SetDragDropPayload("SKYSEAT", &index, sizeof(index));
                if (figure != nullptr)
                    DrawDragged(*figure);
                ImGui::EndDragDropSource();
            }
            const bool menu = ImGui::IsItemClicked(ImGuiMouseButton_Right) && occupied;
            ImGui::PopID();
            if (menu) // outside the seat's ID scope: the menu is drawn by DrawSeatMenu at the page's level
            {
                m_seatMenu = int(i);
                ui::OpenMenu("##seatmenu");
            }
            const bool choosing = m_choosing == int(i), ringed = choosing || target;
            draw->AddRectFilled(at, end, CardFill(ringed || hovered || nav), ui::radius::Card);
            draw->AddRect(at, end,
                ui::Col(ringed    ? ui::color::AccentFill
                        : hovered ? ui::color::Edge
                                  : ui::color::Hairline),
                ui::radius::Card, 0, ringed ? ui::stroke::Edge : ui::stroke::Hairline);
            const int seatVertices = draw->VtxBuffer.Size;
            const ImVec2 picture(at.x + SeatPad, at.y + SeatPad);
            // Three lines beside the picture: the seat (leaving room for the X at the corner), the figure's name, its level.
            const float tx = picture.x + SeatPicture + ui::space::S, room = end.x - tx - SeatPad;
            const bool cross = occupied && editable && !(figure != nullptr && SeatBusy(*figure));
            const float crossRoom = room - (cross ? ui::space::L : 0);
            const float top =
                at.y + std::round((SeatHeight - (2 * ui::type::Caption + ui::type::Body + 2 * ui::space::XXS)) / 2);
            const float second = top + ui::type::Caption + ui::space::XXS,
                        third = second + ui::type::Body + ui::space::XXS;
            PutText(draw, ImVec2(tx, top), ui::TextStyle::Caption, false, choosing ? ui::color::Info : ui::color::Dim,
                SeatName(i), crossRoom);
            if (m_model.RealSeat(i))
                DrawRealSeat(draw, i, at, end);
            else if (occupied && figure != nullptr)
            {
                DrawSeatPicture(draw, *figure, picture, SeatPicture);
                PutText(draw, ImVec2(tx, second), ui::TextStyle::Body, true, ui::color::Ink, Name(*figure), room);
                if (std::string detail = Detail(*figure); !detail.empty())
                    PutText(draw, ImVec2(tx, third), ui::TextStyle::Caption, false,
                        Busy(*figure) ? ui::color::Success : ui::color::Dim, detail, room);
            }
            else
            {
                // Empty, or a figure the collection no longer has (deleted, moved): the seat can still be emptied.
                const ImVec2 box(picture.x + SeatPicture, picture.y + SeatPicture);
                // An empty seat's +: Add a figure for it (the rest of the seat chooses among the shelf's figures).
                bool plus = false;
                if (!occupied && editable)
                {
                    ImGui::SetCursorScreenPos(picture);
                    ImGui::PushID(int(i) + 200);
                    ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true); // the pad's X on the seat does it
                    if (ui::Hit("##seat-add", ImVec2(SeatPicture, SeatPicture)))
                        OpenAdd(int(i));
                    ImGui::PopItemFlag();
                    plus = ImGui::IsItemHovered();
                    ui::Tooltip(Tr("Add a figure"));
                    ImGui::PopID();
                }
                const bool lit = choosing || plus;
                draw->AddRectFilled(picture, box, ui::Col(ui::color::Inset), ui::radius::Control);
                draw->AddRect(picture, box, ui::Col(lit ? ui::color::AccentFill : ui::color::Hairline),
                    ui::radius::Control, 0, ui::stroke::Edge);
                ui::DrawIcon(draw, ui::Icon::Plus, ImVec2(picture.x + SeatPicture / 2, picture.y + SeatPicture / 2),
                    ui::icon::Normal, ui::Col(lit ? ui::color::AccentFill : ui::color::Dim));
                PutText(draw, ImVec2(tx, second), ui::TextStyle::Body, true,
                    occupied ? ui::color::Danger : ui::color::InkSoft, occupied ? Tr("Missing figure") : Tr("Empty"),
                    room);
                PutText(draw, ImVec2(tx, third), ui::TextStyle::Caption, false, ui::color::Dim,
                    choosing   ? Tr("Pick one below")
                    : editable ? Tr("Choose")
                               : "",
                    room);
            }
            if (refuses)
                FadeVertices(draw, seatVertices, RefusedAlpha);
            if (cross)
            {
                ImGui::SetCursorScreenPos(ImVec2(end.x - ui::height::Small, at.y));
                ImGui::PushID(int(i) + 100);
                ImGui::PushItemFlag(ImGuiItemFlags_NoNav,
                    true); // the pointer's: the pad and the keyboard take it off with X and Delete on the seat
                const bool off = ui::IconButton(
                    "##off", ui::Icon::Cross, Tr("Take it off"), { ui::ButtonKind::Ghost, ui::Size::Small });
                ImGui::PopItemFlag();
                const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                m_crossAt[i] = ImVec2((a.x + b.x) / 2, (a.y + b.y) / 2);
                ImGui::PopID();
                if (off)
                    TakeOff(i);
            }
        }
        DrawSeatMenu();
        ImGui::SetCursorScreenPos(
            ImVec2(row.x, row.y + float(rows.size()) * (SeatHeight + SeatGap) - SeatGap + ui::space::M));
    }

    // The bar: what the shelf is for (a seat being chosen for), the search, the order and Filters.
    void CollectionPage::DrawBar()
    {
        const collection::Game* game = m_model.Game();
        const bool choosing = m_choosing >= 0 && game != nullptr;
        const int active = (m_filter.element >= 0) + (!m_filter.game.empty() || m_allGames) + (m_filter.category >= 0);
        const std::string seat = choosing ? Tf("Choose a figure for {}", SeatName(size_t(m_choosing))) : std::string();
        ui::ButtonOptions cancel;
        const float cancelWidth = choosing ? ui::ButtonWidth(Tr("Cancel"), cancel) + ui::space::S : 0;
        if (choosing && !m_searchChoosing && active == 0 && m_search[0] == 0)
        {
            // Choosing for a seat: the banner takes the bar's row, so the shelf stays put; its search brings the bar back.
            const ImVec2 at = ImGui::GetCursorScreenPos();
            const float right = at.x + ImGui::GetContentRegionAvail().x, middle = at.y + ui::height::Normal / 2;
            const float searchX = right - cancelWidth - ui::height::Normal;
            const float room = searchX - ui::space::S - at.x;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            PutText(draw, ImVec2(at.x, middle - ui::type::Body / 2), ui::TextStyle::Body, true, ui::color::Info,
                seat.c_str(), room);
            ui::PushText(ui::TextStyle::Body, true);
            const float after = ImGui::CalcTextSize(seat.c_str()).x + ui::space::S;
            ui::PopText();
            const std::string only = Tf("only what {} reads", ShortGameName(m_model.gameId));
            ui::PushText(ui::TextStyle::Caption);
            const bool explained = after + ImGui::CalcTextSize(only.c_str()).x <= room;
            ui::PopText();
            if (explained)
                PutText(draw, ImVec2(at.x + after, middle - ui::type::Caption / 2), ui::TextStyle::Caption, false,
                    ui::color::InkSoft, only.c_str(), room - after);
            ImGui::SetCursorScreenPos(ImVec2(searchX, at.y));
            if (ui::IconButton("##choosesearch", ui::Icon::Search, Tr("Search")))
                m_searchChoosing = m_focusSearch = true;
            ImGui::SameLine(0, ui::space::S);
            if (ui::Button(Tr("Cancel"), cancel))
                Choose(-1);
            return;
        }

        // The search first and wide, then Sort and filter, one button that says the sort and how many filters are on;
        // when a filter is on, its chip, Clear all and the count of figures found.
        auto buttonText = [](collection::Sort sort, int count) {
            const std::string text = Tf("Sort: {}", SortText(sort));
            return count > 0 ? fmt::format("{} ({})", text, count) : text;
        };
        ui::ButtonOptions button;
        button.trailing = ui::Icon::ChevronDown;
        button.tooltip = Tr("Sort and filter");
        // As wide as its longest text, so that neither it nor the search moves.
        float width = 0;
        for (int sort = 0; sort < int(collection::Sort::Count); sort++)
            width = std::max(width, ui::ButtonWidth(buttonText(collection::Sort(sort), FilterCount).c_str(), button));
        button.width = width;
        const float searchWidth = std::max(
            ui::height::SettingControl / 2, ImGui::GetContentRegionAvail().x - width - ui::space::S - cancelWidth);
        const int count = FoundCount();
        const std::string hint = choosing     ? seat
                                 : count == 0 ? std::string(Tr("Search"))
                                 : count == 1 ? Tf("Search {} figure", count)
                                              : Tf("Search {} figures", count);
        if (m_focusSearch)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusSearch = false;
        }
        ui::SearchField("##search", m_search, sizeof(m_search), hint.c_str(), searchWidth);
        ImGui::SameLine(0, ui::space::S);

        // One id whatever it says: the focus stays on it when the sort changes.
        const bool pressed = ui::Button((buttonText(m_filter.sort, active) + "###sortandfilter").c_str(), button);
        if (pressed || m_openFilters)
            ui::OpenMenu("##filtersmenu", { .alignRight = true });
        m_openFilters = false;
        if (choosing)
        {
            ImGui::SameLine(0, ui::space::S);
            if (ui::Button(Tr("Cancel"), cancel))
                Choose(-1);
        }
        DrawFilters(active);
        DrawTags();
    }

    // The Sort and filter panel: Sort by, a divider, then "Show only" (Element, Works in and Type, each a Select) and
    // Clear all.
    void CollectionPage::DrawFilters(int active)
    {
        ImGui::SetNextWindowSize(ImVec2(FilterPanel, 0));
        if (!ui::BeginMenu("##filtersmenu"))
            return;
        if (m_closeFilters)
            ImGui::CloseCurrentPopup();
        m_closeFilters = false;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui::space::S, ui::space::S));
        // The labels' column fits the widest one (a longer translation never touches its box).
        ui::PushText(ui::TextStyle::Caption);
        float column = 0;
        for (const char* label : { N_("Sort by"), N_("Element"), N_("Works in"), N_("Type") })
            column = std::max(column, ImGui::CalcTextSize(Tr(label)).x + ui::space::M);
        ui::PopText();
        const float room = ImGui::GetContentRegionAvail().x;
        auto row = [&](const char* label, const char* id, const std::string& value,
                       const std::function<void()>& items) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            PutText(ImGui::GetWindowDrawList(),
                ImVec2(at.x + ui::space::XS, at.y + (ui::height::Normal - ui::type::Caption) / 2),
                ui::TextStyle::Caption, false, ui::color::InkSoft, Tr(label), column);
            ImGui::SetCursorScreenPos(ImVec2(at.x + column, at.y));
            ui::SelectOptions options;
            options.width = room - column;
            if (ui::BeginSelect(id, value.c_str(), options))
            {
                items();
                ui::EndSelect();
            }
        };
        row(N_("Sort by"), "##sortby", SortText(m_filter.sort), [&] {
            for (int sort = 0; sort < int(collection::Sort::Count); sort++)
                if (ui::SelectOption(SortText(collection::Sort(sort)).c_str(), m_filter.sort == collection::Sort(sort)))
                    ChangeSort(collection::Sort(sort));
        });
        ui::MenuSeparator();
        ImGui::Indent(ui::space::XS);
        ui::Text(ui::TextStyle::Caption, Tr("Show only"), ui::color::Dim, -1);
        ImGui::Unindent(ui::space::XS);
        row(N_("Element"), "##element",
            m_filter.element >= 0 ? ElementName(collection::Element(m_filter.element)) : Tr("All elements"), [&] {
                if (ui::SelectOption(Tr("All elements"), m_filter.element < 0))
                    m_filter.element = -1;
                for (collection::Element element : MenuElements)
                    if (ui::SelectOption(ElementName(element).c_str(), m_filter.element == int(element)))
                        m_filter.element = int(element);
            });
        const bool shownGame = m_filter.game.empty() && !m_allGames;
        const std::string worksIn = shownGame    ? ShortGameName(m_model.gameId)
                                    : m_allGames ? std::string(Tr("All games"))
                                                 : ShortGameName(m_filter.game);
        row(N_("Works in"), "##game", worksIn, [&] {
            if (ui::SelectOption(Tr("All games"), m_allGames))
            {
                m_filter.game.clear();
                m_allGames = true;
            }
            for (const collection::Game& g : collection::Catalogue::Get().Games())
                if (ui::SelectOption(
                        ShortGameName(g.id).c_str(), shownGame ? g.id == m_model.gameId : m_filter.game == g.id))
                {
                    // The shown game is the shelf's default, not a filter.
                    m_filter.game = g.id == m_model.gameId ? std::string() : g.id;
                    m_allGames = false;
                }
        });
        row(N_("Type"), "##type", m_filter.category >= 0 ? CategoryLabel(Category(m_filter.category)) : Tr("All types"),
            [&] {
                if (ui::SelectOption(Tr("All types"), m_filter.category < 0))
                    m_filter.category = -1;
                // The types the collection has.
                for (int category = 0; category < int(Category::Count); category++)
                    if (std::any_of(m_model.figures.begin(), m_model.figures.end(),
                            [&](const Figure& f) { return f.toy != nullptr && int(f.toy->category) == category; }))
                        if (ui::SelectOption(CategoryLabel(Category(category)).c_str(), m_filter.category == category))
                            m_filter.category = category;
            });
        ui::ButtonOptions clear;
        clear.width = -1;
        clear.disabled = active == 0 && m_search[0] == 0;
        if (ui::Button(Tr("Clear all"), clear))
            ClearFilters();
        ImGui::PopStyleVar();
        ui::EndMenu();
    }

    void CollectionPage::ClearFilters()
    {
        m_search[0] = 0;
        ShelfFilter cleared;
        cleared.sort = m_filter.sort; // the order is not a filter
        m_filter = cleared;
        m_allGames = false;
    }

    // What is on, as chips (a click turns one off; the search is the field's own text), Clear all, and how many figures are found.
    void CollectionPage::DrawTags()
    {
        struct Tag
        {
            std::string label;
            unsigned color;
            int filter;
        };
        std::vector<Tag> tags;
        if (m_filter.element >= 0)
            tags.push_back({ ElementName(collection::Element(m_filter.element)),
                ElementInk(collection::Element(m_filter.element)), 0 });
        if (!m_filter.game.empty())
            tags.push_back({ Tf("Works in {}", ShortGameName(m_filter.game)), 0, 1 });
        if (m_allGames)
            tags.push_back({ Tr("All games"), 0, 3 });
        if (m_filter.category >= 0)
            tags.push_back({ CategoryLabel(Category(m_filter.category)), 0, 2 });
        if (tags.empty() && m_search[0] == 0)
            return;

        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        int remove = -1;
        for (const Tag& tag : tags)
        {
            ui::ChipOptions options;
            options.selected = true;
            options.color = tag.color;
            if (ui::Chip(tag.label.c_str(), options))
                remove = tag.filter;
            ImGui::SameLine(0, ui::space::XS);
        }
        ui::ButtonOptions clear;
        clear.kind = ui::ButtonKind::Ghost;
        clear.size = ui::Size::Small;
        if (ui::Button(Tr("Clear all"), clear))
            ClearFilters();
        const std::string count = FigureCountText(FoundCount());
        ui::PushText(ui::TextStyle::Caption);
        const float countWidth = ImGui::CalcTextSize(count.c_str()).x;
        ui::PopText();
        ImGui::SameLine(0, 0);
        ImGui::SetCursorScreenPos(
            ImVec2(right - countWidth, ImGui::GetCursorScreenPos().y + (ui::height::Small - ui::type::Caption) / 2));
        ui::Text(ui::TextStyle::Caption, count.c_str(), ui::color::InkSoft, -1);
        switch (remove)
        {
        case 0: m_filter.element = -1; break;
        case 1: m_filter.game.clear(); break;
        case 2: m_filter.category = -1; break;
        case 3: m_allGames = false; break;
        default: break;
        }
    }

    int CollectionPage::FoundCount() const
    {
        int count = 0;
        for (const Section& section : m_sections)
            count += int(section.items.size());
        return count;
    }

    bool CollectionPage::DrawCard(const Figure& figure, float width, bool dim)
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float art = width - 2 * ui::space::S;
        const float height = ui::space::S + art + ui::space::XS + 2 * ui::type::Caption + ui::space::XXS + ui::space::S;
        const bool busy = Busy(figure);
        ImGui::PushID(figure.path.c_str());
        ImGui::SetNextItemAllowOverlap(); // the star over its corner takes its own clicks
        const bool pressed = ui::Hit("##figure", ImVec2(width, height));
        if (m_focusCard)
        {
            ui::FocusLastItem(true);
            m_focusCard = false;
        }
        const ui::State state = ui::ItemState();
        const bool hovered = state.hovered, focused = ImGui::IsItemFocused();
        ui::FocusRingIf(state, ui::radius::Card);
        const bool rightClick = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        if (hovered || focused)
            m_hovered = figure.path;
        if (focused)
            m_selected = figure.path;
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip))
        {
            ImGui::SetDragDropPayload("SKYFIG", figure.path.c_str(), figure.path.size() + 1);
            DrawDragged(figure);
            ImGui::EndDragDropSource();
        }
        // Under the pointer only, and not while the pad is moving the focus (the pointer rests on a card): the legend says what
        // the buttons do. Its text is made for that card only, not for every card on every frame.
        const bool bubble = hovered && !ImGui::GetIO().NavVisible;
        if (bubble)
        {
            std::string tip = Name(figure);
            if (dim)
                tip += "\n" + Tf("Not for {}", FullGameName(m_model.gameId));
            else if (busy)
                tip += "\n" + Tf("On the portal in {}: read-only", LiveGame(figure));
            else if (m_choosing >= 0)
                tip += "\n" + Tf("Click: put it on {}", SeatName(size_t(m_choosing)));
            else
                tip += std::string("\n") + Tr("Click: sheet · drag to a seat");
            ui::Bubble(tip.c_str(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }
        if (hovered && m_app.m_testPadWalk)
            m_app.m_drawn["figure-hover"] = bubble ? "bubble" : "none";
        ImGui::PopID();
        // A click (Enter, A): onto the seat being chosen for, else the figure's sheet.
        if (pressed)
        {
            m_selected = figure.path;
            if (dim)
                Toast(Tf("{} is not for {}", Name(figure), FullGameName(m_model.gameId)), ui::ToastKind::Error);
            else if (m_choosing >= 0)
                PutOn(size_t(m_choosing), figure.path);
            else
                OpenFigure(figure.path);
        }
        if (rightClick)
            OpenFigure(figure.path);
        if (!ImGui::IsItemVisible()) // scrolled out of view: a big collection draws only what shows
            return pressed;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(at.x + width, at.y + height), picture(at.x + ui::space::S, at.y + ui::space::S);
        const int seat = m_model.SeatOf(figure.path);
        const bool selected = m_selected == figure.path && focused;
        draw->AddRectFilled(at, end, CardFill(hovered), ui::radius::Card);
        draw->AddRect(at, end,
            ui::Col(seat >= 0             ? ui::color::AccentFill
                    : selected || hovered ? ui::color::Edge
                                          : ui::color::Hairline),
            ui::radius::Card, 0, seat >= 0 || selected ? ui::stroke::Edge : ui::stroke::Hairline);
        if (const collection::Card card = CardOf(figure); !DrawSwapper(draw, card, picture, art))
        {
            DrawIcon(draw, figure.toy, figure.Kind(), picture, art, false, figure.variant, HeldVillain(figure));
            { // the busts end flat: fade their foot into the card
                const ImVec2 top(picture.x, picture.y + art - ui::space::L), foot(picture.x + art, picture.y + art + 1);
                const ImU32 clear = ui::Col(hovered ? ui::color::Raised : ui::color::Surface, 0),
                            solid = CardFill(hovered);
                draw->AddRectFilledMultiColor(top, foot, clear, clear, solid, solid);
            }
            if (card.Swapper())
                DrawBottom(draw, *card.second, ImVec2(picture.x + art * 0.62f, picture.y + art * 0.62f), art * 0.38f);
        }

        // The name (cut with "..." when it is long: the tooltip has it whole), then its level.
        const float room = width - 2 * ui::space::XS, textY = picture.y + art + ui::space::XS;
        auto centred = [&](ui::TextStyle style, bool bold, unsigned color, const std::string& text, float y) {
            ui::PushText(style, bold);
            const std::string cut = ui::Ellipsize(text.c_str(), room);
            const float w = ImGui::CalcTextSize(cut.c_str()).x;
            ui::DrawText(draw, ImVec2(at.x + (width - w) / 2, y), ui::Col(color), cut.c_str());
            ui::PopText();
        };
        // Marks, as on the Toy Box's cards: the element and type icons top left; the seat, then the favorite star, top
        // right. The version is in the name.
        const collection::Card card = CardOf(figure);
        DrawCornerIcons(draw, figure.toy, card.Swapper() ? card.second->toy : figure.toy, figure.battleClass,
            ImVec2(at.x + ui::space::XS, at.y + ui::space::XS), ui::icon::Normal);
        // The favorite star, in the top right corner: empty, filled once the figure is a favorite; a click toggles it (the pad
        // has it on the figure's sheet).
        const ImVec2 starAt(end.x - ui::space::M, at.y + ui::space::M);
        const float starSize = ui::icon::Small;
        // Its button exists only under the pointer: the pad's walk and the grid's layout never see it.
        bool starred = false, starHovered = false;
        if (ImGui::IsMouseHoveringRect(at, end))
        {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(starAt.x - starSize, starAt.y - starSize));
            ImGui::PushID(figure.path.c_str());
            ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
            starred = ImGui::InvisibleButton("##favourite", ImVec2(2 * starSize, 2 * starSize));
            starHovered = ImGui::IsItemHovered();
            ImGui::PopItemFlag();
            ImGui::PopID();
            if (starHovered && !ImGui::GetIO().NavVisible)
                ui::Bubble(figure.favourite ? Tr("Remove from favorites") : Tr("Add to favorites"),
                    ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::SetCursorScreenPos(cursor);
        }
        if (starred && !busy)
            m_toggleFavourite =
                figure.path; // after the shelf is drawn: reloading now would pull the figures from under it
        if (figure.favourite)
        {
            // Lucide's star is an outline: its inside filled under it (the star's points on a 24-unit grid).
            ImVec2 points[10];
            for (int p = 0; p < 10; p++)
            {
                const float angle = -IM_PI / 2 + p * IM_PI / 5, radius = (p % 2 == 0 ? 10.0f : 4.2f) * starSize / 24;
                points[p] = ImVec2(
                    starAt.x + radius * std::cos(angle), starAt.y + 0.6f * starSize / 24 + radius * std::sin(angle));
            }
            draw->AddConcavePolyFilled(points, 10, ui::Col(ui::color::Warning));
        }
        ui::DrawIcon(draw, ui::Icon::Star, starAt, starSize,
            ui::Col(figure.favourite || starHovered ? ui::color::Warning : ui::color::Dim));
        if (seat >= 0)
        {
            const std::string tag = SeatTag(size_t(seat));
            const ImVec2 size = ui::BadgeSize(tag.c_str());
            ui::DrawBadge(draw, ImVec2(starAt.x - starSize / 2 - ui::space::XS - size.x, starAt.y - size.y / 2),
                tag.c_str(), ui::color::AccentFill, ui::color::OnAccent);
        }
        if (dim)
            draw->AddRectFilled(
                at, end, ui::Col(ui::color::Scrim, float(ui::elevation::ScrimAlpha) / 255.0f), ui::radius::Card);
        centred(ui::TextStyle::Caption, true, dim ? ui::color::Dim : ui::color::Ink, Name(figure), textY);
        if (std::string detail = Detail(figure); !detail.empty())
            centred(ui::TextStyle::Caption, false, !dim && busy ? ui::color::Success : ui::color::Dim, detail,
                textY + ui::type::Caption + ui::space::XXS);
        return pressed;
    }

    void CollectionPage::DrawList()
    {
        m_hovered.clear();
        bool filtered = m_search[0] != 0 || m_filter.element >= 0 || m_filter.category >= 0 || !m_filter.game.empty();
        if (m_list.empty())
        {
            const bool none = int(m_model.figures.size()) == m_hiddenStarters;
            if (none)
            {
                if (ui::EmptyState(ui::Icon::Plus,
                        Tr("No figures yet. Add one, or drop your figure files on the window."), Tr("Add a figure")))
                    OpenAdd();
                ui::ButtonOptions starters;
                const std::string label =
                    Tf("Create the {} starter pack", GameName(m_model.gameId.c_str(), GameNameForm::Short));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                     (ImGui::GetContentRegionAvail().x - ui::ButtonWidth(label.c_str(), starters)) / 2);
                if (!m_starterNames.empty() && ui::Button(label.c_str(), starters))
                    CreateStarterPack();
            }
            else if (m_choosing < 0 && !filtered && !m_allGames)
            {
                // Every figure is another game's.
                if (ui::EmptyState(ui::Icon::Search, Tr("No figure matches"), Tr("All games")))
                    m_allGames = true;
            }
            else if (ui::EmptyState(ui::Icon::Search,
                         m_choosing >= 0 && !filtered ? Tr("Nothing for this seat yet") : Tr("No figure matches"),
                         filtered ? Tr("Clear search and filters") : nullptr))
                ClearFilters();
            return;
        }

        const float available = ImGui::GetContentRegionAvail().x;
        const int columns = std::max(1, int((available + ShelfGap) / (ShelfCell + ShelfGap)));
        const float cell = (available - ShelfGap * float(columns - 1)) / float(columns);
        for (size_t s = 0; s < m_sections.size(); s++)
        {
            const Section& section = m_sections[s];
            // A titled sort's sections and the unknown toys have their title and count; one shelf has none (the bar says
            // what it shows). No divider over the first: the bar is right above.
            if (section.kind != Section::Kind::Shelf)
            {
                const std::string count = FigureCountText(int(section.items.size()));
                ui::SectionHeader(SectionName(section).c_str(), count.c_str(), s > 0);
                ImGui::Dummy(ImVec2(0, ui::space::XXS));
            }
            ImGui::PushID(int(s));
            for (size_t i = 0; i < section.items.size(); i++)
            {
                if (i % columns != 0)
                    ImGui::SameLine(0, ShelfGap);
                const Figure& figure = m_list[section.items[i]];
                DrawCard(figure, cell, m_model.Incompatible(figure));
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, ui::space::XS));
        }
        ImGui::Dummy(ImVec2(0, ui::space::L)); // the last row is never cut flush with the shelf's edge
    }

    void CollectionPage::HandleShortcuts()
    {
        if (!m_toggleFavourite.empty()) // a card's star clicked while the shelf was drawn
        {
            if (const collection::Figure* figure = m_model.Find(m_toggleFavourite))
                collection::SetFavourite(m_toggleFavourite, !figure->favourite);
            m_toggleFavourite.clear();
            Reload();
        }
        // The pad's actions, declared every frame: their hints are the legend's (false under a dialog, a menu or a field).
        // X takes the focused seat's figure off (Delete), opens Add a figure for a focused empty seat, else opens the focused
        // card's sheet (F2); Y the search (F3); LT / RT choose for the seat before or after (Page Up / Page Down).
        const collection::Game* seatsOf = m_model.Game();
        const bool seatFocused = m_navSeat >= 0 && size_t(m_navSeat) < m_model.seats.size();
        const bool onSeat = seatFocused && !m_model.seats[size_t(m_navSeat)].empty();
        const bool takeOff = onSeat && ui::PadAction(ui::PadButton::West, Tr("Take it off"), nullptr, ImGuiKey_Delete);
        const bool addHere = seatFocused && !onSeat && m_model.CanEditSeats() && !m_model.RealSeat(size_t(m_navSeat)) &&
                             ui::PadAction(ui::PadButton::West, Tr("Add a figure"));
        const bool sheet =
            !onSeat && m_navSeat < 0 && ui::PadAction(ui::PadButton::West, Tr("Figure sheet"), "F2", ImGuiKey_F2);
        const bool search = ui::PadAction(ui::PadButton::North, Tr("Search"), "F3", ImGuiKey_F3);
        bool before = false, after = false;
        if (seatsOf != nullptr && !seatsOf->seats.empty())
        {
            before = ui::PadAction(ui::PadButton::LeftTrigger, Tr("Other seat"), "PgUp", ImGuiKey_PageUp);
            after = ui::PadAction(ui::PadButton::RightTrigger, Tr("Other seat"), nullptr, ImGuiKey_PageDown);
        }
        if (m_overlay != Overlay::None || ImGui::IsAnyItemActive())
            return;
        auto pressed = [](ImGuiKey a, ImGuiKey b) {
            return ImGui::IsKeyPressed(a, false) || ImGui::IsKeyPressed(b, false);
        };
        if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
            return;
        if (takeOff)
        {
            TakeOff(size_t(m_navSeat));
            return;
        }
        if (addHere)
        {
            OpenAdd(m_navSeat);
            return;
        }
        if (m_hoverSeat >= 0 && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        {
            TakeOff(size_t(m_hoverSeat));
            return;
        }
        if (seatsOf != nullptr && !seatsOf->seats.empty())
        {
            int last = int(seatsOf->seats.size()) - 1;
            if (before)
                Choose(std::max(0, (m_choosing < 0 ? 1 : m_choosing) - 1));
            if (after)
                Choose(std::min(last, m_choosing + 1));
        }
        if (search)
        {
            m_focusSearch = true;
            m_searchChoosing = m_choosing >= 0; // the banner gives its row to the field
        }
        if (sheet)
        {
            const std::string& path = !m_hovered.empty() ? m_hovered : m_selected;
            if (!path.empty() && m_model.Find(path) != nullptr)
                OpenFigure(path);
        }
        // B, Escape: stop choosing, then clear the search.
        if (pressed(ImGuiKey_Escape, ImGuiKey_GamepadFaceRight))
        {
            if (m_choosing >= 0)
                Choose(-1);
            else if (m_search[0] != 0)
                m_search[0] = 0;
        }
    }
}
