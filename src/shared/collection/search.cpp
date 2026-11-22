#include "search.h"
#include "../figures/figure_tag.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace collection
{
    namespace
    {
        // U+00C0..U+00FF without their accents (Latin-1 letters; the multiplication and division signs become spaces).
        constexpr char Latin1[] = "aaaaaaaceeeeiiiidnooooo ouuuuyts"
                                  "aaaaaaaceeeeiiiidnooooo ouuuuyty";

        // The words of folded text: runs of letters and digits.
        std::vector<std::string> Words(const std::string& folded)
        {
            std::vector<std::string> words;
            std::string word;
            for (char c : folded)
            {
                if (isalnum(uint8_t(c)) || (uint8_t(c) >= 0x80))
                    word += c;
                else if (!word.empty())
                {
                    words.push_back(word);
                    word.clear();
                }
            }
            if (!word.empty())
                words.push_back(word);
            return words;
        }

        // At most one insertion, deletion, substitution or swap of two neighbours between the two.
        bool WithinOne(const std::string& a, const std::string& b)
        {
            if (a == b)
                return true;
            size_t la = a.size(), lb = b.size();
            if (la > lb + 1 || lb > la + 1)
                return false;
            size_t i = 0;
            while (i < la && i < lb && a[i] == b[i])
                i++;
            if (la == lb)
            {
                if (a.compare(i + 1, std::string::npos, b, i + 1, std::string::npos) == 0)
                    return true; // one letter differs
                return i + 1 < la && a[i] == b[i + 1] && a[i + 1] == b[i] &&
                       a.compare(i + 2, std::string::npos, b, i + 2, std::string::npos) == 0;
            }
            const std::string& longer = la > lb ? a : b;
            const std::string& shorter = la > lb ? b : a;
            return longer.compare(i + 1, std::string::npos, shorter, i, std::string::npos) == 0;
        }

        // A word of 5 letters or more may have one typo, also against the start of a longer word (the rest is yet to type).
        bool Typo(const std::string& term, const std::string& word)
        {
            if (term.size() < 5)
                return false;
            if (WithinOne(term, word))
                return true;
            return word.size() > term.size() && WithinOne(term, word.substr(0, term.size()));
        }

        // The words of a name: anywhere inside one ("ill" finds Gill), or a typo of one.
        bool NameHit(const std::string& term, const std::vector<std::string>& words)
        {
            for (const std::string& word : words)
                if (word.find(term) != std::string::npos || Typo(term, word))
                    return true;
            return false;
        }

        // The words of a family (elements, games, toy types, versions): the start of one, or a typo of one.
        bool ConceptHit(const std::string& term, const std::set<std::string>& words)
        {
            for (const std::string& word : words)
                if ((term.size() >= 2 && word.compare(0, term.size(), term) == 0) || Typo(term, word))
                    return true;
            return false;
        }

        void AddWords(std::set<std::string>& into, const char* list)
        {
            for (const std::string& word : Words(list))
                into.insert(word);
        }

        // Folded words, English and French, in the order of the enums.
        constexpr const char* ElementWords[] = { "air", "earth terre", "fire feu", "water eau", "magic magie",
            "tech technologie", "life vie", "undead mort vivant", "light lumiere", "dark tenebres ombre", "" };
        static_assert(std::size(ElementWords) == size_t(Element::Count));
        constexpr const char* CategoryWords[] = { "skylander skylanders character characters personnage personnages",
            "giant giants geant geants", "swapper swappers swap top haut", "swapper swappers swap bottom bas",
            "trapmaster trapmasters trap master masters maitre maitres", "supercharger superchargers super charger",
            "sensei senseis", "mini minis sidekick sidekicks acolyte acolytes compagnon", "trap traps piege pieges",
            "villain villains vilain mechant mechants", "vehicle vehicles vehicule vehicules",
            "creation crystal crystals cristal cristaux", "item items objet objets",
            "adventure adventures aventure aventures pack packs location", "trophy trophies trophee trophees" };
        static_assert(std::size(CategoryWords) == size_t(Category::Count));
        constexpr const char* KindWords[] = { "standard", "legendary legendaire", "dark sombre",
            "crystal clear cristal transparent", "glow lueur phosphorescent", "metallic metal metallise", "gold dore",
            "silver argent argente", "chrome", "flocked floque", "series serie", "lightcore", "eon eons elite",
            "alt alternate deco", "event evenement exclusive exclusif", "shape forme", "" };
        static_assert(std::size(KindWords) == size_t(VariantKind::Count));

        const char* GameWords(const std::string& id)
        {
            static const std::map<std::string, const char*> Words = { { "ssa", "ssa spyro spyros" },
                { "giants", "giants giant geants geant" }, { "swapforce", "swapforce swap force" },
                { "trapteam", "trapteam trap team" }, { "superchargers", "superchargers supercharger super chargers" },
                { "imaginators", "imaginators imaginator" } };
            auto it = Words.find(id);
            return it != Words.end() ? it->second : id.c_str();
        }

        // The elements in the order the games' menus show them.
        constexpr Element ElementOrder[] = { Element::Magic, Element::Earth, Element::Water, Element::Air,
            Element::Fire, Element::Life, Element::Undead, Element::Tech, Element::Light, Element::Dark };

        // The Category sort's families, in its order.
        constexpr Category Families[] = { Category::Core, Category::Mini, Category::CreationCrystal, Category::Trap,
            Category::Vehicle, Category::MagicItem, Category::Adventure, Category::Trophy, Category::Villain };

        Category FamilyOf(Category category) { return category <= Category::Sensei ? Category::Core : category; }

        template <typename T, size_t N>
        int IndexIn(const T (&order)[N], T value)
        {
            return int(std::find(order, order + N, value) - order);
        }

        // Where a titled sort puts a figure: its section's place and title.
        struct Place
        {
            int rank = 0;
            Section title;
        };

        Place PlaceOf(const Figure& figure, Sort sort, const std::string& first)
        {
            const Toy& toy = *figure.toy;
            if (sort == Sort::Game)
            {
                const Catalogue& catalogue = Catalogue::Get();
                const Game* origin = catalogue.Origin(toy, figure.variant);
                if (origin == nullptr) // a game the catalogue does not list: after those it does
                    return { int(catalogue.Games().size()), { Section::Kind::Game, 0, toy.game, {} } };
                const int rank = origin->id == first ? -1 : int(origin - catalogue.Games().data());
                return { rank, { Section::Kind::Game, 0, origin->id, {} } };
            }
            int rank = 0;
            if (sort == Sort::Element)
            {
                rank = IndexIn(ElementOrder, toy.element);
                if (rank < int(std::size(ElementOrder)))
                    return { rank, { Section::Kind::Element, int(toy.element), {}, {} } };
            }
            const Category family = FamilyOf(toy.category);
            return { rank + IndexIn(Families, family), { Section::Kind::Category, int(family), {}, {} } };
        }

        // The shelf's cards cut into a titled sort's sections, each keeping the shelf's order.
        std::vector<Section> Cut(
            const std::vector<Figure>& list, const std::vector<size_t>& shelf, Sort sort, const std::string& first)
        {
            std::map<std::pair<int, std::string>, Section> byPlace;
            for (size_t i : shelf)
            {
                Place place = PlaceOf(list[i], sort, first);
                std::pair<int, std::string> key(place.rank, place.title.game);
                byPlace.try_emplace(std::move(key), std::move(place.title)).first->second.items.push_back(i);
            }
            std::vector<Section> sections;
            sections.reserve(byPlace.size());
            for (auto& [key, section] : byPlace)
                sections.push_back(std::move(section));
            return sections;
        }

        // "level 10" is two words; these spell the first.
        bool IsLevelWord(const std::string& word)
        {
            return word == "level" || word == "niveau" || word == "lvl" || word == "lv" || word == "niv";
        }

        // Words that stand for two: folded and joined by single spaces, so "Magic item" is the toy type and not an element.
        std::vector<std::string> Phrases(const std::vector<std::string>& words)
        {
            static const std::pair<const char*, const char*> Replace[] = { { " magic item ", " item " },
                { " magic items ", " items " }, { " objet magique ", " objet " }, { " objets magiques ", " objets " },
                { " trap master ", " trapmaster " }, { " trap masters ", " trapmasters " },
                { " swap force ", " swapforce " }, { " light core ", " lightcore " },
                { " creation crystal ", " creation " }, { " creation crystals ", " creation " } };
            std::string text = " ";
            for (const std::string& word : words)
                text += word + " ";
            for (const auto& [from, to] : Replace)
            {
                for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at))
                {
                    text.replace(at, strlen(from), to);
                    at += strlen(to) - 1;
                }
            }
            return Words(text);
        }
    }

    std::string Fold(const std::string& utf8)
    {
        std::string out;
        for (size_t i = 0; i < utf8.size();)
        {
            uint8_t c = uint8_t(utf8[i]);
            if (c < 0x80)
            {
                out += char(tolower(c));
                i++;
            }
            else if (c == 0xC3 && i + 1 < utf8.size() && (uint8_t(utf8[i + 1]) & 0xC0) == 0x80)
            {
                out += Latin1[uint8_t(utf8[i + 1]) & 0x3F];
                i += 2;
            }
            else if (c == 0xC5 && i + 1 < utf8.size() && (uint8_t(utf8[i + 1]) == 0x92 || uint8_t(utf8[i + 1]) == 0x93))
            {
                out += "oe";
                i += 2;
            }
            else if (c == 0xE2 && i + 2 < utf8.size() && uint8_t(utf8[i + 1]) == 0x80 &&
                     (uint8_t(utf8[i + 2]) == 0x98 || uint8_t(utf8[i + 2]) == 0x99))
            {
                out += '\''; // a typographic apostrophe
                i += 3;
            }
            else
            {
                out += char(c);
                i++;
            }
        }
        return out;
    }

    bool SearchMatches(const std::string& query, const Card& card, const std::string& game)
    {
        std::vector<std::string> terms = Phrases(Words(Fold(query)));
        if (terms.empty())
            return true;

        // What the card is called, and what it is.
        std::vector<std::string> names;
        std::set<std::string> concepts;
        for (const Figure* figure : { card.first, card.second })
        {
            if (figure == nullptr)
                continue;
            for (const std::string& text :
                { figure->label, figures::NicknameUtf8(figure->stats), figure->DisplayName(), figure->variantInfo.name,
                    figure->edition, figure->toy != nullptr ? figure->toy->name : std::string(), figure->file })
                for (const std::string& word : Words(Fold(text)))
                    names.push_back(word);
            if (const Toy* toy = figure->toy)
            {
                AddWords(concepts, ElementWords[size_t(std::min(toy->element, Element::None))]);
                AddWords(concepts, CategoryWords[size_t(toy->category)]);
                AddWords(concepts, KindWords[size_t(toy->edition)]);
                AddWords(concepts, GameWords(toy->game));
            }
            AddWords(concepts, KindWords[size_t(figure->Kind())]);
        }
        if (card.Swapper())
            AddWords(concepts, "swapper swappers swap");

        // The level the card shows, and the level the figure has.
        int shown = card.first->level, own = card.first->level;
        if (!game.empty())
            shown = std::min(shown, figures::LevelCap(game));
        const bool hasLevel = HasProgress(*card.first) && card.first->state != figures::AreaState::Blank;
        auto levelIs = [&](int level) { return hasLevel && (level == shown || level == own); };

        for (size_t i = 0; i < terms.size(); i++)
        {
            const std::string& term = terms[i];
            if (IsLevelWord(term))
            {
                // "level 10"; "level" alone: any toy that has a level.
                if (i + 1 < terms.size() && isdigit(uint8_t(terms[i + 1][0])))
                {
                    if (!levelIs(atoi(terms[i + 1].c_str())))
                        return false;
                    i++;
                }
                else if (!HasProgress(*card.first))
                    return false;
                continue;
            }
            // "level10", "niveau10", "lv10".
            size_t digits = term.find_first_of("0123456789");
            if (digits != std::string::npos && digits > 0 && IsLevelWord(term.substr(0, digits)) &&
                term.find_first_not_of("0123456789", digits) == std::string::npos)
            {
                if (!levelIs(atoi(term.c_str() + digits)))
                    return false;
                continue;
            }
            if (!NameHit(term, names) && !ConceptHit(term, concepts))
                return false;
        }
        return true;
    }

    const char* SortId(Sort sort)
    {
        static const char* const Ids[] = { "game", "recent", "element", "category", "name", "level", "favorites" };
        static_assert(std::size(Ids) == size_t(Sort::Count));
        return sort < Sort::Count ? Ids[size_t(sort)] : Ids[size_t(DefaultSort)];
    }

    Sort ParseSort(const std::string& id)
    {
        for (int i = 0; i < int(Sort::Count); i++)
            if (id == SortId(Sort(i)))
                return Sort(i);
        return DefaultSort;
    }

    std::string SortLabel(Sort sort, bool french)
    {
        static const char* const English[] = { "Game", "Recent", "Element", "Category", "Name", "Level", "Favorites" };
        static const char* const France[] = { "Jeu", "Récents", "Élément", "Catégorie", "Nom", "Niveau", "Favoris" };
        static_assert(std::size(English) == size_t(Sort::Count) && std::size(France) == size_t(Sort::Count));
        return sort < Sort::Count ? (french ? France : English)[size_t(sort)] : "";
    }

    std::string FigureCount(int count, bool french)
    {
        if (french)
            return std::to_string(count) + (count > 1 ? " figurines" : " figurine");
        return std::to_string(count) + (count == 1 ? " figure" : " figures");
    }

    std::vector<Section> Shelf(
        const std::vector<Figure>& list, Sort sort, const std::string& game, const std::string& first)
    {
        std::vector<Section> sections = Group(list, GroupBy::Smart, OrderBy::Recent, game);
        if (sort == Sort::Recent)
            return sections;
        // Group leaves an empty shelf out, and puts it first.
        if (sections.empty() || sections.front().kind != Section::Kind::Shelf)
            return sections;

        if (sort == Sort::Game || sort == Sort::Element || sort == Sort::Category)
        {
            std::vector<Section> cut = Cut(list, sections.front().items, sort, first.empty() ? game : first);
            cut.insert(cut.end(), sections.begin() + 1, sections.end());
            return cut;
        }

        std::vector<size_t>& items = sections.front().items;
        if (sort == Sort::Favorites) // the Recent order otherwise: the sort is stable
        {
            std::stable_sort(items.begin(), items.end(),
                [&](size_t a, size_t b) { return list[a].favourite && !list[b].favourite; });
            return sections;
        }

        std::map<std::string, size_t> byFile;
        for (size_t i = 0; i < list.size(); i++)
            byFile[list[i].file] = i;
        auto nameOf = [&](size_t i) {
            const Figure& f = list[i];
            auto other = f.partner.empty() ? byFile.end() : byFile.find(f.partner);
            return Fold(CardName(other != byFile.end() ? Card{ &f, &list[other->second] } : Card{ &f, nullptr }));
        };
        std::map<size_t, std::string> names;
        for (size_t i : items)
            names[i] = nameOf(i);
        auto levelOf = [&](size_t i) { return HasProgress(list[i]) ? list[i].level : -1; };
        std::stable_sort(items.begin(), items.end(), [&](size_t a, size_t b) {
            if (sort == Sort::Level && levelOf(a) != levelOf(b))
                return levelOf(a) > levelOf(b);
            return names[a] < names[b];
        });
        return sections;
    }
}
