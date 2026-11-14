// Synthetic UI checks: requirement labels fit in every language; icon glyphs use the framebuffer's pixel density.
#include "launcher/i18n.h"
#include "launcher/game_names.h"
#include "launcher/ui/collection_page.h"
#include "launcher/ui/components/lucide.h"
#include "shared/embedded_fonts.h"
#include <SDL3/SDL.h>
#include <imgui_impl_sdlrenderer3.h>
#include <stb_image_write.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

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
    ui::Ctx().fonts = savedFonts;
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui::DestroyContext();
    ImGui::SetCurrentContext(previous);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
    printf("UI rendering: all games, categories and languages; icons at 1x, 1.5x, 2x and back to 1x checked\n");
    return failures;
}
