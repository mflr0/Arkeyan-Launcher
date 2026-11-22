#pragma once

// Home's big pieces: Banner (what to do next) and GameCard (a game, its art, its name and its state).
#include "launcher/ui/components/buttons.h"
#include "launcher/ui/components/feedback.h"
#include <functional>

namespace ui
{
    // ---- Banner: a heading, two lines under it, the main action at their right. The button is the last item drawn. ----
    struct BannerOptions
    {
        const char* heading = "";
        const char* title = "";
        const char* detail = "";
        bool bigTitle = false; // a name (type::Title), else a sentence (type::Body)
        const char* action = "";
        const char* actionId = "##banner";
        ButtonOptions button;
    };
    float BannerHeadingHeight();
    float BannerBlockHeight(); // the whole block, with the air under it
    bool Banner(ImVec2 at, float width, const BannerOptions& options);

    // ---- GameCard: GameCardButton is the item (act on it, or focus it, right after), DrawGameCard what is seen. ----
    struct GameCardState
    {
        bool pressed = false;
        bool hovered = false;
        bool focused = false;
    };
    struct GameCardOptions
    {
        ImTextureRef art; // the painting, cropped to the card (artSize: its pixels); none: the two fills
        ImVec2 artSize;
        float artDim = 0;
        ImU32 fillTop = 0, fillBottom = 0;
        ImU32 hoverEdge = 0;
        std::function<void(ImVec2 at, ImVec2 size)> logo; // none: the name as a word
        const char* name = "";
        float nameSize = type::Display;
        const char* state = "";
        Status status = Status::Neutral;
        Icon stateIcon = Icon::None;
    };
    GameCardState GameCardButton(const char* id, ImVec2 at, ImVec2 size);
    void DrawGameCard(ImVec2 at, ImVec2 size, const GameCardState& state, const GameCardOptions& options);
    // The size all the names of a row of cards share.
    float GameCardNameSize(const char* name, float cardWidth);
}
