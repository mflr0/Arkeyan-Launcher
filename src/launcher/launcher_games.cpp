#include "launcher/game_names.h"
#include "launcher/launcher.h"

// Hashes identify the player's own default.xex and its loaded image (decrypted, decompressed); they are not game content.
namespace
{
    const GameRelease SpyrosAdventureReleases[] =
    {
        // Redump "Skylanders - Spyro's Adventure (USA, Europe) (En,Fr,De,Es,It,Nl,Sv,No,Da,Fi)", XGD3, 8738846720 bytes.
        // SHA-256 of its default.xex (12595200 bytes), computed from the player's image.
        { "USA, Europe", 0x415608AB, 0x53CE7B97, 0x00000002, "e195f5130c1e23fb4654baf7c383ef97d02c89aeb7d4389705ae31235646ef7b",
            "f53c42d4e3575f56d39ffa2428bcf86923b7dd80e5b59927b86cf070b33b4c62" },
    };

    const GameRelease GiantsReleases[] =
    {
        // Redump "Skylanders - Giants (USA, Europe) (En,Fr,De,Es,It,Nl,Pt,Sv,No,Da,Fi)", XGD3, 8738846720 bytes.
        // SHA-256 of its default.xex (7020544 bytes), computed from the player's image.
        { "USA, Europe", 0x415608DA, 0x52FFE94B, 0x00000002, "12d1e29e4f07fbc559bc1a4988dd6bf346c3d16e30cbf862e2683e862f9fc09e",
            "43a64a6d9795408e48fd39cb655361dc1be4ef9f22f60ddc09621139aa4c4b17" },
    };

    const GameRelease SwapForceReleases[] =
    {
        // Redump "Skylanders - Swap Force (USA, Australia) (En,Fr)", XGD3, 8738846720 bytes.
        // SHA-256 of its default.xex (18563072 bytes), computed from the player's image.
        { "USA, Australia", 0x415608E9, 0x22F1FB91, 0x00000021, "5d9a3bc3c2d85ed59684a9c5f94daf6bffcfdfedcef7650a866737edbf3f4ac3",
            "f6648b7ea47b43136e82e2b7dab5935018e86983179c4db6ecef177c5a1edf26" },
        // Redump "Skylanders - Swap Force (Europe) (Es,It)", XGD3, 8738846720 bytes: the same game code, another XEX header.
        { "Europe", 0x415608E9, 0x74FDDE06, 0x0000001C, "3db455f6ee17e8cb3a4d58873d96583d9160396d360a0130e5eb88adad8a5518",
            "f6648b7ea47b43136e82e2b7dab5935018e86983179c4db6ecef177c5a1edf26" },
    };

    const GameRelease TrapTeamReleases[] =
    {
        // Redump "Skylanders - Trap Team (Europe) (En,De)" and "(Fr,Nl)", 8738846720 bytes: the same loaded image, only the XEX header differs.
        { "Europe", 0x41560911, 0x7A74A451, 0x00000014, "f1d2373e4507e21cc371e6f22d02e909602a1b5bc4704896c3c099cc64002a35",
            "265c807d93716525e611839fb30859c1eb8a2a7cc700074d57f7b4cb3f78d458" },
        { "Europe (Fr, Nl)", 0x41560911, 0x3AF0B6AF, 0x00000012, "95e273f479ad71198d7daaa6234e6ee6364861ce29b002ba782154a1fcf08d3c",
            "265c807d93716525e611839fb30859c1eb8a2a7cc700074d57f7b4cb3f78d458" },
    };

    const GameRelease SuperChargersReleases[] =
    {
        // Redump "Skylanders - SuperChargers (Europe)", 8738846720 bytes.
        { "Europe", 0x4156091F, 0x74144F53, 0x00000007, "02ff6672d460c9aaa54e69d584af67a7138c6c93cde5e04b0da4284207be7c61",
            "f2c26c0153255c100bd5f1baceec7f2d3f26903f79047a0dc6ceb51d9a8e70bc" },
    };

    const GameRelease ImaginatorsReleases[] =
    {
        // Redump "Skylanders - Imaginators (Europe) (Fr,Nl)", 8738846720 bytes.
        // SHA-256 of its default.xex (25739264 bytes), computed from the player's image.
        { "Europe", 0x41560930, 0x0133F901, 0x00000004, "1c6d7e05ef6d30ba0b86ff3a810ba115bef3050998254471230cd8c07641d0ac",
            "4f793c38af18299cc2dd71a062dcd5ab5e9c4fb8b42769b791bd947a32859d02" },
    };

    // `runtime` is the executable the player kit builds for the game (its config/<id>/game.txt).
    const GameInfo Games[] = {
        { "ssa", GameName("ssa"), SpyrosAdventureReleases, "Skylanders Spyro's Adventure", "SSARecomp" },
        { "giants", GameName("giants"), GiantsReleases, "Skylanders Giants", "SGRecomp" },
        { "swapforce", GameName("swapforce"), SwapForceReleases, "Skylanders Swap Force", "SSFRecomp" },
        { "trapteam", GameName("trapteam"), TrapTeamReleases, "Skylanders Trap Team", "TTRecomp" },
        { "superchargers", GameName("superchargers"), SuperChargersReleases, "Skylanders SuperChargers" },
        { "imaginators", GameName("imaginators"), ImaginatorsReleases, "Skylanders Imaginators" },
    };
}

const std::span<const GameInfo> g_games = Games;
const char* const g_launcherName = "SSARecomp";

const GameInfo* FindGame(std::string_view id)
{
    for (const GameInfo& game : g_games)
    {
        if (id == game.id)
            return &game;
    }
    return nullptr;
}

const GameInfo* FindEnabledGame(std::string_view id, const LauncherConfig& config)
{
    (void)config; // every game in the table is shown; an old [launcher] experimental_superchargers line is ignored
    return FindGame(id);
}
