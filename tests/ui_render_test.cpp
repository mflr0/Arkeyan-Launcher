// Synthetic UI checks: requirement labels fit in every language; icon glyphs use the framebuffer's pixel density.
#include "launcher/i18n.h"
#include "launcher/game_names.h"
#include "launcher/ui/collection_page.h"
#include "launcher/ui/components/feedback.h"
#include "launcher/ui/components/inputs.h"
#include "launcher/ui/components/layout.h"
#include "launcher/ui/launcher_ui.h"
#include "launcher/ui/theme/launcher_theme.h"
#include "launcher/ui/components/lucide.h"
#include "shared/embedded_fonts.h"
#include <SDL3/SDL.h>
#include <imgui_impl_sdlrenderer3.h>
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

int UiRenderChecks(const std::filesystem::path& scratch)
{
    int failures = 0;
    auto expect = [&](bool ok, const char* what) {
        if (!ok)
        {
            fprintf(stderr, "FAIL UI rendering: %s\n", what);
            ++failures;
        }
    };
    std::filesystem::create_directories(scratch);
    SDL_Surface* surface = SDL_CreateSurface(1024, 256, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* renderer = surface != nullptr ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    expect(renderer != nullptr, "software renderer created");
    if (renderer == nullptr)
    {
        SDL_DestroySurface(surface);
        return failures;
    }
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(512, 128);
    const ui::Fonts savedFonts = ui::Ctx().fonts;
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.OversampleH = config.OversampleV = 1;
    config.PixelSnapH = true;
    ImFont* text = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(g_embeddedOutfit), int(g_embeddedOutfitSize), ui::type::Body, &config);
    ImFont* icons = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(g_embeddedLucide), int(g_embeddedLucideSize), ui::icon::Normal, &config);
    ui::Ctx().fonts = { text, text, text, text, icons };
    ImGui_ImplSDLRenderer3_Init(renderer);
    const std::string language = i18n::Current();
    const auto& catalogue = collection::Catalogue::Get();
    // Reuse the font across density changes, including a return from 2x to 1x.
    int frame = 0;
    for (float density : { 1.0f, 1.5f, 2.0f, 1.0f })
    {
        io.DisplayFramebufferScale = ImVec2(density, density);
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("UI rendering", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImFont* current = ImGui::GetFont();
        const float currentSize = ImGui::GetFontSize();
        int row = 0;
        for (float size : { ui::icon::Small, ui::icon::Normal, ui::icon::Large })
        {
            int column = 0;
            for (ui::Icon icon : { ui::Icon::Search, ui::Icon::Gear, ui::Icon::Folder, ui::Icon::Plus, ui::Icon::Check,
                     ui::Icon::Cross })
            {
                ui::DrawIcon(draw, icon, ImVec2(32.0f + column * 64, 24.0f + row * 40), size, IM_COL32_WHITE);
                ++column;
            }
            ImFontBaked* baked = icons->GetFontBaked(size);
            expect(baked->RasterizerDensity == density, "icons rasterized at the current display density");
            const ImFontGlyph* glyph = baked->FindGlyph(ui::lucide::Search);
            expect(glyph != nullptr && glyph->Visible, "icon glyph exists");
            if (glyph != nullptr)
            {
                const float pixels = (glyph->V1 - glyph->V0) * baked->OwnerFont->OwnerAtlas->TexData->Height;
                expect(pixels >= size * density / 2, "glyph bitmap has enough physical pixels");
            }
            expect(ImGui::GetFont() == current && ImGui::GetFontSize() == currentSize,
                "drawing an icon preserves the surrounding text font");
            ++row;
        }
        // A small empty seat (SuperChargers' seven rows) keeps its plus at half the circle, as at full size.
        {
            const int first = draw->VtxBuffer.Size;
            const float seat = 24;
            ui::DrawEmptySeat(draw, ImVec2(400, 40), seat);
            float top = 1e9f, bottom = -1e9f;
            for (int v = first; v < draw->VtxBuffer.Size; v++)
                if (draw->VtxBuffer[v].col == ui::Col(ui::color::Ink))
                {
                    top = std::min(top, draw->VtxBuffer[v].pos.y);
                    bottom = std::max(bottom, draw->VtxBuffer[v].pos.y);
                }
            expect(bottom > top && bottom - top <= seat / 2 + 1, "an empty seat's plus scales with its circle");
        }
        for (const auto& lang : i18n::Languages())
        {
            i18n::SetLanguage(lang.code);
            const float room = launcher_ui::AddToyMinCell() - 2 * ui::space::XS;
            ui::PushText(ui::TextStyle::Caption);
            for (const auto& game : catalogue.Games())
            {
                bool found = false;
                for (const auto& toy : catalogue.Toys())
                {
                    if (toy.game != game.id)
                        continue;
                    found = true;
                    const std::string label = launcher_ui::AddToyRequirement(toy);
                    expect(label == i18n::Tf("needs {}", GameName(game.id, GameNameForm::Short)),
                        "every game's figures and categories keep the requirement wording");
                    expect(
                        ImGui::CalcTextSize(label.c_str()).x <= room, "the full translated requirement fits the card");
                }
                expect(found, "each catalogue game has figures checked");
            }
            ui::PopText();
        }
        i18n::SetLanguage(language);
        ImGui::End();
        ImGui::Render();
        SDL_SetRenderScale(renderer, density, density);
        SDL_SetRenderDrawColor(renderer, 28, 28, 33, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        const std::string name = "icons-" + std::to_string(frame++) + ".png";
        expect(stbi_write_png((scratch / name).string().c_str(), int(512 * density), int(128 * density), 4,
                   surface->pixels, surface->pitch) != 0,
            "icon screenshot written");
    }
    // The tab bar at the smallest window, with a pad: the caps name the buttons that change tab, and the row fits in every
    // language, the right cap included.
    {
        constexpr float ColumnW = 672; // a settings page's content at 1024 wide, beside the sidebar and the scrollbar
        const ui::Glyphs savedGlyphs = ui::Ctx().glyphs;
        io.DisplayFramebufferScale = ImVec2(1, 1);
        io.DisplaySize = ImVec2(1024, 256);
        auto drawTabs = [&](std::span<const char* const> labels, ui::Glyphs glyphs, auto&& check) {
            ImGui_ImplSDLRenderer3_NewFrame();
            ImGui::NewFrame();
            ui::Ctx().glyphs = glyphs;
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(ColumnW, 128));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin("Tabs", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const int first = draw->VtxBuffer.Size;
            int current = 0;
            ui::Tabs("##testtabs", labels, &current);
            check(draw, first);
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::Render();
        };
        const char* const plain[] = { "One", "Six" }; // no B and no T: those glyphs come from the caps alone
        drawTabs(plain, ui::Glyphs::Xbox, [&](ImDrawList* draw, int first) {
            ImFontBaked* baked = text->GetFontBaked(ui::type::Caption);
            auto count = [&](ImWchar c) {
                const ImFontGlyph* glyph = baked->FindGlyph(c);
                int found = 0;
                for (int v = first; v < draw->VtxBuffer.Size; v++)
                    found += draw->VtxBuffer[v].uv.x == glyph->U0 && draw->VtxBuffer[v].uv.y == glyph->V0;
                return found;
            };
            expect(count('T') == 2 && count('B') == 0, "the tab caps name LT and RT, the buttons that change tab");
        });
        const char* const game[] = { "Picture", "Controls", "Sound and language", "Manage" };
        const char* const launcher[] = { "General", "Controls", "Portal", "Help" };
        for (const auto& lang : i18n::Languages())
        {
            i18n::SetLanguage(lang.code);
            for (std::span<const char* const> source :
                { std::span<const char* const>(game), std::span<const char* const>(launcher) })
            {
                std::vector<const char*> labels;
                for (const char* label : source)
                    labels.push_back(i18n::Tr(label));
                for (ui::Glyphs glyphs : { ui::Glyphs::Xbox, ui::Glyphs::PlayStation, ui::Glyphs::Nintendo })
                    drawTabs(labels, glyphs, [&](ImDrawList* draw, int first) {
                        float right = 0;
                        for (int v = first; v < draw->VtxBuffer.Size; v++)
                            right = std::max(right, draw->VtxBuffer[v].pos.x);
                        if (right > ColumnW)
                            fprintf(stderr, "tabs %s (%d labels): %.0f of %.0f points\n", lang.code.c_str(),
                                int(labels.size()), right, ColumnW);
                        expect(right <= ColumnW, "the tabs and both caps fit the smallest window");
                    });
            }
        }
        i18n::SetLanguage(language);
        // A game's pages wear its colour: the tab's line, the step in progress and the bar.
        const unsigned savedAccent = ui::Ctx().accent;
        constexpr unsigned GameColour = 0x12B456;
        ui::Ctx().accent = GameColour;
        auto coloured = [&](ImDrawList* draw, int first) {
            for (int v = first; v < draw->VtxBuffer.Size; v++)
                if (draw->VtxBuffer[v].col == ui::Col(GameColour))
                    return true;
            return false;
        };
        drawTabs(plain, ui::Glyphs::Keyboard, [&](ImDrawList* draw, int first) {
            expect(coloured(draw, first), "the selected tab's line wears the game's colour");
            int from = draw->VtxBuffer.Size;
            ui::GuideStep(1, "One", "Six", 300, true);
            expect(coloured(draw, from), "the step in progress wears the game's colour");
            from = draw->VtxBuffer.Size;
            ui::ProgressBar(0.5f, 300, false);
            expect(coloured(draw, from), "a progress bar wears the game's colour");
        });
        ui::Ctx().accent = savedAccent;
        // The one fold: what it holds shows only open, lined up under the title; a summary or an action keeps the row's height.
        drawTabs(plain, ui::Glyphs::Keyboard, [&](ImDrawList*, int) {
            bool closed = false, opened = true;
            const float left = ImGui::GetCursorScreenPos().x;
            float at = ImGui::GetCursorScreenPos().y;
            expect(!ui::BeginDisclosure("##closed", "One", &closed), "a closed disclosure draws nothing under it");
            const float bare = ImGui::GetCursorScreenPos().y - at;
            ui::DisclosureOptions full;
            full.summary = "Six";
            full.action = "One";
            at = ImGui::GetCursorScreenPos().y;
            expect(!ui::BeginDisclosure("##full", "One", &closed, full), "a closed row with an action stays closed");
            expect(ImGui::GetCursorScreenPos().y - at == bare, "every disclosure row is as tall");
            if (ui::BeginDisclosure("##open", "One", &opened))
            {
                expect(ImGui::GetCursorScreenPos().x == left, "what is unfolded lines up under the title");
                ui::EndDisclosure();
            }
            else
                expect(false, "an open disclosure draws what is under it");
        });
        // The sidebar's badges are their games' colours, Imaginators the deep blue of its card beside Spyro's sky blue.
        for (const LauncherGameEntry& entry : LauncherGameCatalog({}))
        {
            if (!entry.enabled)
                continue;
            const LauncherTheme::GameStyle& style = LauncherTheme::StyleOf(entry.id);
            const unsigned wanted = std::string(entry.id) == "imaginators" ? style.bottom : style.glow;
            expect(entry.accent == wanted, "a game's badge has the game's colour");
            expect(ui::Contrast(entry.accent, ui::InkOn(entry.accent, LauncherTheme::BadgeInk)) >= 4.5f,
                "a badge's initials read on its colour");
        }
        ui::Ctx().glyphs = savedGlyphs;
    }
    ui::Ctx().fonts = savedFonts;
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui::DestroyContext();
    ImGui::SetCurrentContext(previous);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    printf("UI rendering: all games, categories and languages; icons at 1x, 1.5x, 2x and back to 1x checked\n");
    return failures;
}
