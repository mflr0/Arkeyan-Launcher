// Add a figure: the catalogue of the six games, one card per character, the versions in a side panel, "owned x2" marks.
#include "launcher/ui/launcher_app.h"
#include "launcher/ui/launcher_ui.h"
#include <algorithm>
#include <fmt/format.h>

namespace launcher_ui
{
    using collection::Category;
    using collection::Toy;

    namespace
    {
        // A swapper is added whole: the catalogue lists its tops only, named without the "(top)", and creating one makes both halves.
        bool IsSwapper(const Toy& toy) { return toy.category == Category::SwapTop; }

        // The bottom that goes with a swapper's top (the same version's when it has one), for its joined picture.
        const Toy* BottomOf(const Toy& top, uint16_t variant, uint16_t& bottomVariant)
        {
            bottomVariant = 0;
            const Toy* bottom = collection::Catalogue::Get().Find(collection::MatchingHalf(top.type));
            if (bottom != nullptr && variant != 0)
                for (const collection::Variant& candidate : bottom->variants)
                    if (candidate.id == variant)
                        bottomVariant = candidate.id;
            return bottom;
        }

        // The game a toy's versions are told apart in: the chosen game when it reads the toy, else the game the toy came with.
        std::string ViewGame(const std::string& chosen, const Toy& toy)
        {
            return collection::Catalogue::Get().Accepts(chosen, toy.type) ? chosen : toy.game;
        }

        // Every version has a row but the "base" entry (the plain toy, made under the id it is sold with). A version the game
        // does not tell apart (an Eon's Elite Spyro in Spyro's Adventure) is offered too: it plays as the plain toy there and
        // as itself in the games that know it, as the real figure does.
        bool PlainDuplicate(const std::string&, const Toy&, const collection::Variant& variant)
        {
            return variant.kind == collection::VariantKind::Base;
        }

        // A version the chosen game reads as the plain toy (not a same-tag edition, which no game sees).
        bool PlaysAsPlain(const std::string& game, const Toy& toy, const collection::Variant& variant)
        {
            const collection::Catalogue& catalogue = collection::Catalogue::Get();
            return !variant.sameTag && catalogue.Accepts(game, toy.type) &&
                   !catalogue.Read(game, &toy, variant.id).exact;
        }

        struct AddGroup
        {
            std::string title;
            std::vector<const Toy*> toys;
        };

        // The Toy Box's Add sections: the characters by element (Kaos, who has none, last), then the Minis, Traps,
        // Vehicles, Creation Crystals, Magic Items, Adventure Packs and Trophies; any other kind after them. Empty ones
        // are left out.
        std::vector<AddGroup> AddGroups(const std::vector<const Toy*>& toys)
        {
            using collection::Element;
            const auto character = [](const Toy& toy) {
                return toy.category != Category::Mini && toy.category <= Category::Sensei;
            };
            std::vector<AddGroup> groups;
            std::vector<bool> placed(toys.size());
            auto take = [&](std::string title, auto belongs) {
                AddGroup group{ std::move(title), {} };
                for (size_t i = 0; i < toys.size(); i++)
                    if (!placed[i] && belongs(*toys[i]))
                    {
                        placed[i] = true;
                        group.toys.push_back(toys[i]);
                    }
                if (!group.toys.empty())
                    groups.push_back(std::move(group));
            };
            for (Element element : { Element::Magic, Element::Earth, Element::Water, Element::Air, Element::Fire,
                     Element::Life, Element::Undead, Element::Tech, Element::Light, Element::Dark })
                take(ElementName(element), [&](const Toy& toy) { return character(toy) && toy.element == element; });
            take("Kaos", character);
            for (Category category : { Category::Mini, Category::Trap, Category::Vehicle, Category::CreationCrystal,
                     Category::MagicItem, Category::Adventure, Category::Trophy })
                take(CategoryLabel(category), [&](const Toy& toy) { return toy.category == category; });
            for (int category = 0; category < int(Category::Count); category++)
                take(CategoryLabel(Category(category)), [&](const Toy& toy) { return int(toy.category) == category; });
            return groups;
        }

        size_t CountVersions(const std::string& game, const Toy& toy)
        {
            return size_t(std::count_if(toy.variants.begin(), toy.variants.end(),
                [&](const collection::Variant& v) { return !PlainDuplicate(game, toy, v); }));
        }
    }

    std::string AddToyRequirement(const Toy& toy) { return Tf("needs {}", ShortGameName(toy.game)); }

    namespace
    {
        // The wide card's left part, as the Toy Box's: the corner icons' column, then the picture.
        constexpr float AddArt = ui::height::Normal;
        constexpr float AddTextX = ui::space::XS + ui::icon::Small + ui::space::XS + AddArt + ui::space::XS;
        constexpr float AddCardH = AddArt + 2 * ui::space::XS;
    }

    float AddToyMinCell()
    {
        // The longest requirement, else room for "Trap Master · owned x2": two cards side by side in the dialog.
        float width = ui::height::SettingControl / 2 - ui::space::XL;
        ui::PushText(ui::TextStyle::Caption);
        for (const collection::Game& game : collection::Catalogue::Get().Games())
        {
            Toy toy;
            toy.game = game.id;
            width = std::max(width, ImGui::CalcTextSize(AddToyRequirement(toy).c_str()).x);
        }
        ui::PopText();
        return std::ceil(AddTextX + width + ui::space::XS);
    }

    void CollectionPage::DrawAdd()
    {
        DrawAddFilters();

        std::string search = Lower(m_addSearch);
        constexpr float PanelW = ui::height::SettingControl - ui::space::XL;
        // The grid ends above the footer; its scrollbar (always there, so nothing shifts) sits against the last column, and the
        // cards share the width left, so the grid fills its column.
        const float listH =
            OverlayBody(4 * ui::height::Main + ui::height::Normal, 4 * ui::height::Main, 8 * ui::height::Main);
        const float gridW = ImGui::GetContentRegionAvail().x - PanelW - ui::space::S;
        const Toy* chosen = DrawAddGrid(ImVec2(gridW, listH), search);

        DrawAddVersions(chosen, listH);

        DrawAddFooter(chosen);
    }

    void CollectionPage::DrawAddFilters()
    {
        const collection::Catalogue& catalogue = collection::Catalogue::Get();
        // Game, search, category.
        const float room = ImGui::GetContentRegionAvail().x;
        const float selectW = (room - 2 * ui::space::S) / 4;
        ui::SelectOptions gameOptions;
        gameOptions.width = selectW;
        if (ui::BeginSelect("##addgame", Tf("For {}", ShortGameName(m_addGame)).c_str(), gameOptions))
        {
            for (const collection::Game& game : catalogue.Games())
                if (ui::SelectOption(ShortGameName(game.id).c_str(), game.id == m_addGame))
                {
                    m_addGame = game.id;
                    m_addToy = 0xFFFF;
                }
            ui::EndSelect();
        }
        ImGui::SameLine(0, ui::space::S);
        ui::SearchField("##addsearch", m_addSearch, sizeof(m_addSearch), Tr("Search the catalog"),
            room - 2 * selectW - 2 * ui::space::S);
        // Type: the same menu as the shelf's bar.
        ImGui::SameLine(0, ui::space::S);
        ui::SelectOptions typeOptions;
        typeOptions.width = selectW;
        if (ui::BeginSelect("##addtype",
                m_addCategory < 0 ? Tr("All types") : CategoryLabel(Category(m_addCategory)).c_str(), typeOptions))
        {
            if (ui::SelectOption(Tr("All types"), m_addCategory < 0))
                m_addCategory = -1;
            for (int category = 0; category < int(Category::Count); category++)
                if (std::any_of(catalogue.Toys().begin(), catalogue.Toys().end(),
                        [&](const Toy& toy) { return int(toy.category) == category; }))
                    if (ui::SelectOption(CategoryLabel(Category(category)).c_str(), m_addCategory == category))
                        m_addCategory = category;
            ui::EndSelect();
        }
    }

    int CollectionPage::FirstAddMatchingVersion(const Toy& toy, const std::string& search) const
    {
        // The first matching version is 1-based; 0 selects the toy itself.
        if (search.empty())
            return 0;
        for (size_t i = 0; i < toy.variants.size(); i++)
            if (!PlainDuplicate(ViewGame(m_addGame, toy), toy, toy.variants[i]) &&
                Lower(toy.variants[i].name).find(search) != std::string::npos)
                return int(i) + 1;
        return 0;
    }

    bool CollectionPage::MatchesAddToy(const Toy& toy, const std::string& search) const
    {
        if (toy.category == Category::SwapBottom)
            return false; // the bottom comes with its top
        if (m_addCategory >= 0 && int(toy.category) != m_addCategory)
            return false;
        return search.empty() || Lower(toy.name).find(search) != std::string::npos ||
               FirstAddMatchingVersion(toy, search) > 0;
    }

    int CollectionPage::CountAddOwned(uint16_t type, int variant, const std::string& edition) const
    {
        // Owned copies exclude untouched starters.
        int count = 0;
        for (const collection::Figure& figure : m_model.figures)
            if (figure.type == type && (variant < 0 || figure.variant == variant) &&
                (edition.empty() || figure.edition == edition) && !collection::UntouchedStarter(figure))
                count++;
        return count;
    }

    bool CollectionPage::DrawAddToy(const Toy& toy, int pass, float cell, const std::string& search)
    {
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushID(toy.type);
        if (ui::Hit("##toy", ImVec2(cell, AddCardH)))
        {
            m_addToy = toy.type;
            m_addVersion = FirstAddMatchingVersion(toy, search);
            m_created.clear();
            m_addLabel[0] = 0;
        }
        const ui::State state = ui::ItemState();
        ui::FocusRingIf(state, ui::radius::Card);
        ImGui::PopID();
        const bool hovered = state.hovered, selected = m_addToy == toy.type;
        if (!ImGui::IsItemVisible())
            return selected;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 end(at.x + cell, at.y + AddCardH);
        draw->AddRectFilled(at, end,
            ui::Col(selected  ? ui::color::RaisedPressed
                    : hovered ? ui::color::Raised
                              : ui::color::Inset),
            ui::radius::Card);
        if (selected)
            draw->AddRect(at, end, ui::Col(ui::color::AccentFill), ui::radius::Card, 0, ui::stroke::Edge);
        // The Toy Box's Add card: the element and type icons in a column, the picture, then the name over the type and
        // how many are owned ("Trap Master · owned x2"), or what a later game's toy needs; its versions' count after the
        // name.
        uint16_t bottomVariant = 0;
        const Toy* bottom = IsSwapper(toy) ? BottomOf(toy, 0, bottomVariant) : nullptr;
        DrawCornerIcons(draw, &toy, bottom != nullptr ? bottom : &toy, 0,
            ImVec2(at.x + ui::space::XS, at.y + ui::space::XS), ui::icon::Small);
        const ImVec2 art(at.x + AddTextX - ui::space::XS - AddArt, at.y + ui::space::XS);
        if (bottom == nullptr || !DrawSwapper(draw, &toy, 0, bottom, bottomVariant, art, AddArt))
            DrawIcon(draw, &toy, collection::VariantKind::Base, art, AddArt, false);
        const size_t versions = CountVersions(ViewGame(m_addGame, toy), toy);
        const std::string more = versions > 0 ? "+" + std::to_string(versions) : std::string();
        const float textX = at.x + AddTextX, room = end.x - ui::space::XS - textX;
        const float nameY = at.y + ui::space::XS;
        PutText(draw, ImVec2(textX, nameY), ui::TextStyle::Body, true, ui::color::Ink,
            IsSwapper(toy) ? WithoutHalf(toy.name) : toy.name,
            room - (more.empty() ? 0 : ui::BadgeSize(more.c_str()).x + ui::space::XS));
        const float lineY = end.y - ui::space::XS - ui::type::Caption;
        const int owns = CountAddOwned(toy.type, -1, {});
        std::string line = pass == 1   ? AddToyRequirement(toy)
                           : owns == 0 ? std::string(Tr("not owned"))
                           : owns == 1 ? std::string(Tr("owned"))
                                       : Tf("owned x{}", owns);
        if (const std::string title = collection::CategoryTitle(toy.category); pass == 0 && !title.empty())
            line = std::string(Tr(title.c_str())) + " \xC2\xB7 " + line;
        PutText(draw, ImVec2(textX, lineY), ui::TextStyle::Caption, false,
            pass == 0 && owns > 0 ? ui::color::Success : ui::color::Dim, line, room);
        if (!more.empty()) // it has versions: a small badge, the count
        {
            const ImVec2 size = ui::BadgeSize(more.c_str());
            ui::DrawBadge(draw, ImVec2(end.x - size.x - ui::space::XS, nameY + (ui::type::Body - size.y) / 2),
                more.c_str(), ui::color::AccentFill, ui::color::OnAccent);
        }
        return selected;
    }

    const Toy* CollectionPage::DrawAddGrid(ImVec2 size, const std::string& search)
    {
        const collection::Catalogue& catalogue = collection::Catalogue::Get();
        const auto matches = [&](const Toy& toy) { return MatchesAddToy(toy, search); };
        const bool found = std::any_of(catalogue.Toys().begin(), catalogue.Toys().end(), matches);
        ui::BeginRoomyChild("##addtoys", size, ImGuiChildFlags_NavFlattened,
            ImGuiWindowFlags_NoBackground | (found ? ImGuiWindowFlags_AlwaysVerticalScrollbar : 0));
        ImGui::SetScrollX(0.0f);
        const float MinCell = AddToyMinCell();
        constexpr float Gap = ui::space::S;
        const float usable = ImGui::GetContentRegionAvail().x - ui::space::XS;
        const int columns = std::max(1, int((usable + Gap) / (MinCell + Gap)));
        const float Cell = std::floor((usable + Gap) / columns - Gap);
        const Toy* chosen = nullptr;
        if (!found)
            ui::EmptyState(ui::Icon::Search, Tr("No figure matches"));
        auto cards = [&](const std::vector<const Toy*>& toys, int pass) {
            for (size_t i = 0; i < toys.size(); i++)
            {
                if (i % columns != 0)
                    ImGui::SameLine(0, Gap);
                if (DrawAddToy(*toys[i], pass, Cell, search))
                    chosen = toys[i];
            }
            ImGui::Dummy(ImVec2(0, ui::space::XS));
        };
        // The game's own toys in the Toy Box's sections, then the later games' in one.
        for (int pass = 0; pass < 2; pass++)
        {
            std::vector<const Toy*> toys;
            for (const Toy& toy : catalogue.Toys())
                if (matches(toy) && catalogue.Accepts(m_addGame, toy.type) == (pass == 0))
                    toys.push_back(&toy);
            if (toys.empty())
                continue;
            const std::string count = FigureCountText(int(toys.size()));
            ui::SectionHeader(pass == 0 ? Tf("Works in {}", ShortGameName(m_addGame)).c_str() : Tr("Later games"),
                count.c_str(), pass > 0);
            ImGui::Dummy(ImVec2(0, ui::space::XXS));
            if (pass == 1)
            {
                cards(toys, pass);
                continue;
            }
            for (const AddGroup& group : AddGroups(toys))
            {
                ui::Text(ui::TextStyle::Caption, fmt::format("{} \xC2\xB7 {}", group.title, group.toys.size()).c_str(),
                    ui::color::InkSoft, 0, true);
                cards(group.toys, pass);
            }
        }
        ui::EndRoomyChild();

        return chosen;
    }

    void CollectionPage::DrawAddVersions(const Toy* chosen, float listH)
    {
        constexpr float PanelW = ui::height::SettingControl - ui::space::XL;
        // The versions of the chosen toy, as tall as the grid with its room (the run of its scrollbar).
        ImGui::SameLine(0, ui::space::S);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ui::focus::Reach);
        ui::BeginCard("##addversions", ImVec2(PanelW, listH + 2 * ui::focus::Reach), { .padding = ui::space::XS });
        if (chosen == nullptr)
            ui::Text(ui::TextStyle::Body, Tr("Pick a figure."), ui::color::Dim);
        else
        {
            // The Toy Box's head: the picture, the name, then the element and the type ("Fire · Giant").
            constexpr float Art = ui::height::Main;
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            uint16_t bottomVariant = 0;
            if (!IsSwapper(*chosen) ||
                !DrawSwapper(draw, chosen, 0, BottomOf(*chosen, 0, bottomVariant), bottomVariant, at, Art))
                DrawPicture(draw, chosen, chosen->edition, 0, at, Art);
            const std::string title = collection::CategoryTitle(chosen->category);
            const float textX = at.x + Art + ui::space::XS,
                        room = ImGui::GetContentRegionAvail().x - Art - ui::space::XS;
            PutText(draw, ImVec2(textX, at.y + Art / 2 - ui::type::Body - ui::space::XXS / 2), ui::TextStyle::Body,
                true, ui::color::Ink, IsSwapper(*chosen) ? WithoutHalf(chosen->name) : chosen->name, room);
            PutText(draw, ImVec2(textX, at.y + Art / 2 + ui::space::XXS / 2), ui::TextStyle::Caption, false,
                ui::color::Dim,
                fmt::format("{} \xC2\xB7 {}", ElementName(chosen->element),
                    title.empty() ? CategoryLabel(chosen->category) : std::string(Tr(title.c_str()))),
                room);
            ImGui::Dummy(ImVec2(0, Art + ui::space::XXS));
            std::vector<std::pair<std::string, collection::VariantKind>> versions{
                { IsSwapper(*chosen) ? WithoutHalf(chosen->name) : chosen->name, chosen->edition }
            };
            for (const collection::Variant& variant : chosen->variants)
                versions.push_back({ variant.name, variant.kind });
            m_addVersion = std::clamp(m_addVersion, 0, int(versions.size()) - 1);
            ui::Text(ui::TextStyle::Caption, Tr("Versions"), ui::color::InkSoft, 0, true);
            constexpr float VersionArt = ui::height::Large - 2 * ui::space::XXS;
            for (size_t i = 0; i < versions.size(); i++)
            {
                if (i > 0 && PlainDuplicate(ViewGame(m_addGame, *chosen), *chosen, chosen->variants[i - 1]))
                    continue;
                int count = i == 0 ? CountAddOwned(chosen->type, 0, {}) +
                                         (collection::StandardVariant(*chosen) != 0
                                                 ? CountAddOwned(chosen->type, collection::StandardVariant(*chosen), {})
                                                 : 0)
                            : chosen->variants[i - 1].sameTag
                                ? CountAddOwned(chosen->type, -1, chosen->variants[i - 1].name)
                                : CountAddOwned(chosen->type, chosen->variants[i - 1].id, {});
                // A row: the version's picture (the toy's own when it has none), its name, kind and game, owned count.
                const std::string title = IsSwapper(*chosen) ? WithoutHalf(versions[i].first) : versions[i].first;
                std::string kind = VersionSubtitle(*chosen, i);
                if (i > 0 && PlaysAsPlain(m_addGame, *chosen, chosen->variants[i - 1]))
                    kind +=
                        " · " + Tf("plays as {0} in {1}", IsSwapper(*chosen) ? WithoutHalf(chosen->name) : chosen->name,
                                    ShortGameName(m_addGame));
                const std::string owns = count > 0 ? Tf("owned x{}", count) : std::string();
                ui::ListRowOptions options;
                options.subtitle = kind.c_str();
                options.trailing = owns.empty() ? nullptr : owns.c_str();
                options.selected = m_addVersion == int(i);
                options.imageWidth = VersionArt;
                options.image = [&](ImDrawList* draw, ImVec2 art, ImVec2) {
                    const uint16_t versionId =
                        i == 0 || chosen->variants[i - 1].sameTag ? uint16_t(0) : chosen->variants[i - 1].id;
                    uint16_t bottomVariant = 0;
                    if (!IsSwapper(*chosen) ||
                        !DrawSwapper(draw, chosen, versionId, BottomOf(*chosen, versionId, bottomVariant),
                            bottomVariant, art, VersionArt))
                        DrawPicture(draw, chosen, versions[i].second, i == 0 ? uint16_t(0) : chosen->variants[i - 1].id,
                            art, VersionArt);
                };
                ImGui::PushID(int(i));
                if (ui::ListRow(title.c_str(), options))
                    m_addVersion = int(i);
                ImGui::PopID();
            }
        }
        ui::EndCard();
    }

    void CollectionPage::CreateAddFigure(const Toy* chosen, const std::string& versionName)
    {
        const collection::Variant* variant = m_addVersion > 0 ? &chosen->variants[m_addVersion - 1] : nullptr;
        std::string path;
        if (IsSwapper(*chosen))
        {
            // Both halves, linked: the bottom of the same version when it has one.
            uint16_t bottomType = collection::MatchingHalf(chosen->type), bottomVariant = 0;
            if (const Toy* bottom = collection::Catalogue::Get().Find(bottomType);
                bottom != nullptr && variant != nullptr && !variant->sameTag)
                for (const collection::Variant& candidate : bottom->variants)
                    if (candidate.id == variant->id)
                        bottomVariant = candidate.id;
            path = collection::CreateSwapper(
                chosen->type, bottomType, variant != nullptr && !variant->sameTag ? variant->id : 0, bottomVariant)
                       .top;
        }
        else
            path = collection::Create(chosen->type,
                variant != nullptr ? (variant->sameTag ? collection::StandardVariant(*chosen) : variant->id)
                                   : collection::StandardVariant(*chosen));
        if (path.empty())
            Toast(Tr("Couldn't create the figure"), ui::ToastKind::Error);
        else
        {
            if (variant != nullptr && variant->sameTag)
                collection::SetEdition(path, variant->name, variant->kind);
            if (!collection::CleanLabel(m_addLabel).empty())
                collection::SetLabel(path, m_addLabel);
            m_created = path;
            m_addLabel[0] = 0;
            Reload();
            Toast(Tf("Created {}", versionName), ui::ToastKind::Success);
            // Opened from an empty seat's +: straight onto that seat when it takes the figure, and Add a figure closes.
            const collection::Figure* made = m_model.Find(path);
            if (m_addSeat >= 0 && made != nullptr && m_model.seats.size() > size_t(m_addSeat) &&
                m_model.seats[size_t(m_addSeat)].empty() && Refusal(size_t(m_addSeat), *made).empty())
            {
                PutOn(size_t(m_addSeat), path);
                m_created.clear();
                CloseOverlay();
            }
        }
    }

    void CollectionPage::DrawAddFooter(const Toy* chosen)
    {
        // The footer: a name (optional), what was just made on a seat, then Close and Create.
        ImGui::Dummy(ImVec2(0, ui::space::XXS));
        if (chosen == nullptr)
        {
            if (OverlayActions(Tr("Close")) == ui::ModalResult::Cancel)
                CloseOverlay();
            return;
        }
        std::string versionName =
            m_addVersion > 0 ? chosen->variants[std::min<size_t>(m_addVersion - 1, chosen->variants.size() - 1)].name
                             : std::string(chosen->name);
        if (IsSwapper(*chosen))
            versionName = WithoutHalf(versionName);
        ui::TextFieldOptions labelOptions;
        labelOptions.hint = Tr("Name (optional)");
        labelOptions.width = ui::height::SettingControl - ui::space::XL;
        ui::TextField("##addlabel", m_addLabel, sizeof(m_addLabel), labelOptions);
        if (!m_created.empty())
        {
            const collection::Game* game = m_model.Game();
            const collection::Figure* made = m_model.Find(m_created);
            for (size_t seat = 0; made != nullptr && game != nullptr && seat < game->seats.size(); seat++)
            {
                if (!SeatAccepts(game->seats[seat], *made) ||
                    !collection::Catalogue::Get().Accepts(m_model.gameId, made->type))
                    continue;
                ImGui::SameLine(0, ui::space::S);
                if (ui::Button(Tf("Put it on {}", SeatName(seat)).c_str()))
                {
                    PutOn(seat, m_created);
                    m_created.clear();
                }
                break;
            }
        }
        ImGui::SameLine(0, ui::space::S);
        const std::string create = Tf("Create {}", ui::Ellipsize(versionName.c_str(), ui::height::SettingControl / 2));
        const ui::ModalResult result = OverlayActions(Tr("Close"), create.c_str());
        if (result == ui::ModalResult::Cancel)
            CloseOverlay();
        else if (result == ui::ModalResult::Confirm)
            CreateAddFigure(chosen, versionName);
    }
}
