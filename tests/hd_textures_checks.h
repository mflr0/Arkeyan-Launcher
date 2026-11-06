#pragma once

// HD textures (docs/tools/hd_textures.md): the kit's texture step alone and as the last step of an install, with the fake builder.
inline void HdTexturesChecks(const std::filesystem::path& scratch)
{
    namespace fs = std::filesystem;
    const fs::path root = scratch / "hd-textures";
    fs::remove_all(root);
    fs::create_directories(root);
    auto read = [](const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        std::string text(std::istreambuf_iterator<char>(in), {});
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        return text;
    };
    auto wait = [](GameInstaller& installer) {
        const uint64_t deadline = SDL_GetTicks() + 5000;
        while (installer.Running() && SDL_GetTicks() < deadline) SDL_Delay(5);
        EXPECT(!installer.Running());
        if (installer.Running()) installer.Cancel();
        return installer.Snapshot(3);
    };
    TestSetEnv("FAKE_SSABUILD_DELAY", "0");
    for (const char* key : { "FAKE_SSABUILD_FAIL", "FAKE_SSABUILD_FAIL_STEP", "FAKE_SSABUILD_PAUSE_STEP", "FAKE_SSABUILD_HOLD", "FAKE_SSABUILD_ENDING", "FAKE_SSABUILD_HD_MARKER" })
        TestUnsetEnv(key);
    const auto kit = InstallKitIn(SKYLAUNCHER_FAKE_KIT);
    EXPECT(kit.has_value());
    if (!kit) return;
    const std::string marker = (root / "marker.txt").string();
    TestSetEnv("FAKE_SSABUILD_HD_MARKER", marker.c_str());

    // A kit knows the option when its builder's file names it; an older one (a file that does not) does not.
    EXPECT(kit->hdTextures);
    const fs::path oldKitFolder = root / "old-kit";
    fs::create_directories(oldKitFolder);
    { std::ofstream(oldKitFolder / "Arkeyan-Builder") << "usage: Arkeyan-Builder --iso <disc> --out <dir> [--jobs N]\n"; }
    const auto oldKit = InstallKitIn(oldKitFolder);
#if defined(_WIN32)
    fs::rename(oldKitFolder / "Arkeyan-Builder", oldKitFolder / "Arkeyan-Builder.exe");
    const auto oldKitWindows = InstallKitIn(oldKitFolder);
    EXPECT(oldKitWindows && !oldKitWindows->hdTextures);
#else
    EXPECT(oldKit && !oldKit->hdTextures);
#endif
    const std::optional<InstallKit> tooOld = oldKit ? oldKit : InstallKitIn(oldKitFolder);

    // On, alone: one step, the mode "auto", its own log, and none of an install's housekeeping.
    GameInstaller installer;
    const fs::path game = root / "game";
    fs::create_directories(game);
    EXPECT(installer.StartHdTextures(kit, "synthetic.iso", game, true));
    auto state = wait(installer);
    EXPECT(state.state == InstallProgress::State::Done && !state.startFailed && state.error.empty());
    EXPECT(state.steps.size() == 1 && state.steps[0].id == "hdtextures" && state.done == 40 && state.total == 40);
    EXPECT(read(marker) == "auto" && fs::exists(game / "hd-textures.log") && !fs::exists(game / "install.log"));
    EXPECT(!fs::exists(game / "layout.txt") && !fs::exists(game / "install.prev.log"));
    EXPECT(read(game / "hd-textures.log").find("Making HD textures") != std::string::npos);

    // Off: the same step asked to remove the pack.
    EXPECT(installer.StartHdTextures(kit, "synthetic.iso", game, false));
    state = wait(installer);
    EXPECT(state.state == InstallProgress::State::Done && read(marker) == "off");

    // Stopped while it works: Cancelled, no runtime, and the game's folder holds no install log.
    TestSetEnv("FAKE_SSABUILD_PAUSE_STEP", "1");
    EXPECT(installer.StartHdTextures(kit, "synthetic.iso", game, true));
    const uint64_t deadline = SDL_GetTicks() + 5000;
    do { state = installer.Snapshot(); SDL_Delay(5); } while (!(state.steps.size() == 1 && state.done == 1) && installer.Running() && SDL_GetTicks() < deadline);
    EXPECT(installer.Running() && state.state == InstallProgress::State::Running && state.done == 1);
    installer.Cancel();
    state = wait(installer);
    EXPECT(state.state == InstallProgress::State::Cancelled && state.runtime.empty() && !fs::exists(game / "install.log"));
    TestUnsetEnv("FAKE_SSABUILD_PAUSE_STEP");

    // A failure says why, in the builder's words, and is not a start failure.
    TestSetEnv("FAKE_SSABUILD_FAIL", "hdtextures");
    EXPECT(installer.StartHdTextures(kit, "synthetic.iso", game, true));
    state = wait(installer);
    EXPECT(state.state == InstallProgress::State::Failed && !state.startFailed && state.runtime.empty());
    EXPECT(state.error.find("Making HD textures failed") != std::string::npos);
    TestUnsetEnv("FAKE_SSABUILD_FAIL");

    // A kit too old for the option: nothing starts, and the reason is the launcher's own.
    EXPECT(!installer.StartHdTextures(tooOld, "synthetic.iso", game, true));
    state = installer.Snapshot();
    EXPECT(state.state == InstallProgress::State::Failed && state.startFailed && state.error.find("can't make HD textures") != std::string::npos);
    EXPECT(!installer.Start(tooOld, "synthetic.iso", root / "old-install", true));
    EXPECT(installer.Snapshot().startFailed && !fs::exists(root / "old-install" / "install.log"));
    EXPECT(!installer.StartHdTextures({}, "synthetic.iso", game, true) && installer.Snapshot().startFailed);

    // An install that asks for them ends with their step, which the checklist expects before it comes; one that does not has none.
    const fs::path withPack = root / "install-hd", without = root / "install-plain";
    fs::remove(marker);
    EXPECT(installer.Start(kit, "synthetic.iso", withPack, true));
    state = installer.Snapshot();
    EXPECT(state.hdTextures);
    {
        const InstallChecklist early = MakeInstallChecklist(state, SDL_GetTicks());
        EXPECT(!early.rows.empty() && early.rows.back().id == "hdtextures" && early.rows.back().state == InstallChecklist::RowState::Waiting);
    }
    state = wait(installer);
    EXPECT(state.state == InstallProgress::State::Done && state.steps.size() == 11 && state.steps.back().id == "hdtextures");
    EXPECT(read(marker) == "auto" && fs::is_regular_file(state.runtime) && fs::exists(withPack / "layout.txt"));
    {
        const InstallChecklist done = MakeInstallChecklist(state, SDL_GetTicks());
        EXPECT(done.overall == 1.0f && done.rows.back().id == "hdtextures" && done.rows.back().state == InstallChecklist::RowState::Done);
        EXPECT(done.rows.back().title == "Making HD textures");
    }
    fs::remove(marker);
    EXPECT(installer.Start(kit, "synthetic.iso", without));
    EXPECT(!installer.Snapshot().hdTextures);
    state = wait(installer);
    EXPECT(state.state == InstallProgress::State::Done && state.steps.size() == 10 && !fs::exists(marker));
    {
        const InstallChecklist plain = MakeInstallChecklist(state, SDL_GetTicks());
        EXPECT(std::none_of(plain.rows.begin(), plain.rows.end(), [](const auto& row) { return row.id == "hdtextures"; }));
    }
    TestUnsetEnv("FAKE_SSABUILD_HD_MARKER");
    TestUnsetEnv("FAKE_SSABUILD_DELAY");
    printf("HD textures: on, off, cancel, failure, too-old kit and the install's last step checked\n");
}
