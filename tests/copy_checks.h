#pragma once

#include "launcher/game_copy.h"

inline void SyntheticCopyChecks(const std::filesystem::path& scratch)
{
    namespace fs = std::filesystem;
    using Outcome = gamedata::CopyResult::Outcome;
    const fs::path root = scratch / "synthetic-copy";
    fs::create_directories(root);
    auto remember = [](const char* key) -> std::optional<std::string> {
        if (const char* value = skyenv::Get(key))
            return std::string(value);
        return {};
    };
    auto restore = [](const char* key, const std::optional<std::string>& value) {
        if (value)
            TestSetEnv(key, value->c_str());
        else
            TestUnsetEnv(key);
    };
    const auto previousData = remember("SKYLANDERS_DATA");
    const auto previousLauncher = remember("SKY_LAUNCHER_DATA");
    const auto previousSpace = remember("SKY_LAUNCHER_COPY_FREE_BYTES");
    TestSetEnv("SKYLANDERS_DATA", (root / "data").string().c_str());
    TestSetEnv("SKY_LAUNCHER_DATA", (root / "data").string().c_str());
    const GameInfo& game = *FindGame("ssa");
    const auto folder = gamedata::Folder(game.id);
    const auto image = root / "synthetic.iso";
    const auto payload = RandomBytes(4096);
    WriteDiscImage(image, "payload.bin", payload);
    GameCopyJob job;
    EXPECT(!job.Running() && !job.Done() && job.Fraction() == 0 && job.Bytes() == 0 && job.Total() == 0);
    auto finish = [&] {
        const uint64_t deadline = SDL_GetTicks() + 5000;
        while (!job.Done() && SDL_GetTicks() < deadline)
            SDL_Delay(5);
        EXPECT(job.Running() && job.Done());
        if (!job.Done())
            job.Cancel();
        auto result = job.Collect();
        EXPECT(!job.Running() && !job.Done());
        return result;
    };
    auto pick = PickGameSource(game, image.string());
    EXPECT(!pick.copied && pick.path == image.string() && pick.copy.state == gamedata::State::None);
    TestSetEnv("SKY_LAUNCHER_COPY_FREE_BYTES", "1");
    job.Start(game, image.string(), "synthetic-hash");
    auto result = finish();
    EXPECT(result.outcome == Outcome::NotEnoughSpace && result.available == 1 && result.needed > 4096);
    EXPECT(CopyFailureText(result).starts_with("Not enough space:") && !fs::exists(folder / "payload.bin"));
    TestUnsetEnv("SKY_LAUNCHER_COPY_FREE_BYTES");

    job.Start(game, image.string(), "synthetic-hash", true);
    result = finish();
    EXPECT(result.outcome == Outcome::Cancelled && CopyFailureText(result).empty());
    pick = PickGameSource(game, image.string());
    EXPECT(!pick.copied && pick.path == image.string() && pick.copy.state == gamedata::State::Partial);

    job.Start(game, image.string(), "synthetic-hash");
    result = finish();
    EXPECT(result.outcome == Outcome::Done && CopyFailureText(result).empty());
    EXPECT(job.Bytes() == payload.size() && job.Total() == payload.size() && job.Fraction() == 1);
    auto copiedBytes = [&] {
        std::ifstream in(folder / "payload.bin", std::ios::binary);
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
    };
    EXPECT(copiedBytes() == payload);
    pick = PickGameSource(game, "");
    EXPECT(pick.copied && pick.path == datadir::Text(folder) && pick.copy.state == gamedata::State::Complete &&
           pick.copy.xexSha256 == "synthetic-hash");
    EXPECT(!PickGameSource(*FindGame("giants"), image.string()).copied);

    fs::resize_file(folder / "payload.bin", 1);
    pick = PickGameSource(game, image.string());
    EXPECT(!pick.copied && pick.path == image.string() && pick.copy.state == gamedata::State::Damaged);
    job.Start(game, image.string(), "synthetic-hash");
    EXPECT(finish().outcome == Outcome::Done && gamedata::Inspect(folder).state == gamedata::State::Complete &&
           copiedBytes() == payload);
    job.Start(game, (root / "missing.iso").string(), "synthetic-hash");
    result = finish();
    EXPECT(result.outcome == Outcome::Failed && result.error == "couldn't open the disc image");
    EXPECT(CopyFailureText(result).find(result.error) != std::string::npos);
    EXPECT(gamedata::Inspect(folder).state == gamedata::State::Complete);
    restore("SKYLANDERS_DATA", previousData);
    restore("SKY_LAUNCHER_DATA", previousLauncher);
    restore("SKY_LAUNCHER_COPY_FREE_BYTES", previousSpace);
    printf("Synthetic copy: worker reuse, cancellation/resume, source fallback, repair, space and failure messages "
           "checked\n");
}
