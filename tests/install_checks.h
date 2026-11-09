#pragma once

// Synthetic builder runs: no disc, compiler, downloaded kit or game process is needed.
inline void InstallLifecycleChecks(const std::filesystem::path& scratch)
{
    namespace fs = std::filesystem;
    const fs::path root = scratch / "install-lifecycle";
    fs::create_directories(root);
    auto read = [](const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    auto wait = [](GameInstaller& installer) {
        const uint64_t deadline = SDL_GetTicks() + 5000;
        while (installer.Running() && SDL_GetTicks() < deadline) SDL_Delay(5);
        EXPECT(!installer.Running());
        if (installer.Running()) installer.Cancel();
        return installer.Snapshot(3);
    };
    TestSetEnv("FAKE_ARKEYAN_BUILDER_DELAY", "0");
    for (const char* key : { "FAKE_ARKEYAN_BUILDER_FAIL", "FAKE_ARKEYAN_BUILDER_FAIL_STEP", "FAKE_ARKEYAN_BUILDER_PAUSE_STEP", "FAKE_ARKEYAN_BUILDER_HOLD", "FAKE_ARKEYAN_BUILDER_ENDING" })
        TestUnsetEnv(key);
    const auto kit = InstallKitIn(SKYLAUNCHER_FAKE_KIT);
    EXPECT(kit.has_value());
    if (!kit) return;
    GameInstaller installer;
    EXPECT(installer.Snapshot().state == InstallProgress::State::Idle && installer.ProcessId() == 0);
    EXPECT(ReadInstallLog(root).state == InstallProgress::State::Idle);

    // Every startup refusal must reach the UI with its localized-message flag intact.
    EXPECT(!installer.Start({}, "synthetic.iso", root / "no-kit"));
    auto state = installer.Snapshot(10);
    EXPECT(state.state == InstallProgress::State::Failed && state.startFailed);
    EXPECT(state.error.find("Game kit not found") != std::string::npos && state.logLines == 1 && state.log[0] == state.error);
    EXPECT(InstallFailureHeadline(MakeInstallChecklist(state, 0), state) == "The installer couldn't start.");
    EXPECT(!installer.Running() && installer.ProcessId() == 0);
    { std::ofstream(root / "blocked") << "keep"; }
    EXPECT(!installer.Start(kit, "synthetic.iso", root / "blocked" / "game"));
    state = installer.Snapshot();
    EXPECT(state.startFailed && state.error.find("Couldn't create the game folder") != std::string::npos);
    EXPECT(read(root / "blocked") == "keep");
    InstallKit absent = *kit;
    absent.ssabuild = root / "missing-builder";
    EXPECT(!installer.Start(absent, "synthetic.iso", root / "no-builder"));
    state = installer.Snapshot();
    EXPECT(state.startFailed && state.error.find("Couldn't start the game") != std::string::npos);

    const char* steps[] = { "toolchain", "disc", "recompile", "shaders", "compile", "link", "movies", "shaders", "compile", "link" };
    for (size_t index = 0; index < std::size(steps); ++index)
    {
        const std::string ordinal = std::to_string(index + 1);
        const fs::path out = root / ordinal;
        const bool updating = index % 2 != 0;
        fs::path previousRuntime = out / FindGame("ssa")->runtime;
#if defined(_WIN32)
        previousRuntime += ".exe";
#endif
        fs::create_directories(out / "ssabuild-work");
        { std::ofstream(out / "ssabuild-work" / "keep") << "partial work"; }
        { std::ofstream(out / "save-sentinel") << "player data"; }
        if (updating)
        {
            std::ofstream(previousRuntime) << "previous build";
            MarkGameLayout(out);
        }
        TestSetEnv("FAKE_ARKEYAN_BUILDER_PAUSE_STEP", ordinal.c_str());
        EXPECT(installer.Start(kit, "synthetic.iso", out));
        const uint64_t deadline = SDL_GetTicks() + 5000;
        do
        {
            state = installer.Snapshot(2);
            if (state.steps.size() == index + 1 && state.done == 1) break;
            SDL_Delay(5);
        } while (installer.Running() && SDL_GetTicks() < deadline);
        EXPECT(state.state == InstallProgress::State::Running && state.steps.size() == index + 1 && state.done == 1);
        EXPECT(installer.ProcessId() != 0 && !state.steps.empty() && state.steps.back().id == steps[index]);
        installer.Cancel();
        installer.Cancel();
        state = wait(installer);
        EXPECT(state.state == InstallProgress::State::Cancelled && installer.ProcessId() == 0);
        EXPECT(state.runtime.empty() && fs::exists(out / "layout.txt") == updating && read(out / "ssabuild-work" / "keep") == "partial work");
        EXPECT(!updating || read(previousRuntime) == "previous build");
        auto recovered = ReadInstallLog(out);
        EXPECT(recovered.state == InstallProgress::State::Interrupted && recovered.steps.size() == index + 1);
        EXPECT(!recovered.steps.empty() && recovered.steps.back().id == steps[index] && recovered.done == 1);
        for (const auto& step : recovered.steps) EXPECT(step.startedAt == 0 && step.endedAt == 0);
        TestUnsetEnv("FAKE_ARKEYAN_BUILDER_PAUSE_STEP");

        // Failure at this occurrence, including the second shaders/compile/link passes; retry the same installer.
        TestSetEnv("FAKE_ARKEYAN_BUILDER_FAIL_STEP", ordinal.c_str());
        EXPECT(installer.Start(kit, "synthetic.iso", out));
        state = wait(installer);
        EXPECT(state.state == InstallProgress::State::Failed && !state.startFailed && state.steps.size() == index + 1);
        EXPECT(state.error.find("didn't compile") != std::string::npos && state.runtime.empty());
        EXPECT(ReadInstallLog(out).state == InstallProgress::State::Failed && ReadInstallLog(out).error == state.error);
        EXPECT(fs::exists(out / "ssabuild-work" / "keep") && fs::exists(out / "layout.txt") == updating);
        EXPECT(!updating || read(previousRuntime) == "previous build");
        TestUnsetEnv("FAKE_ARKEYAN_BUILDER_FAIL_STEP");
        const std::string failedLog = read(out / "install.log");
        EXPECT(installer.Start(kit, "synthetic.iso", out));
        state = wait(installer);
        EXPECT(state.state == InstallProgress::State::Done && state.steps.size() == 10 && state.error.empty() && !state.startFailed);
        EXPECT(fs::is_regular_file(state.runtime) && fs::exists(out / "layout.txt") && !fs::exists(out / "ssabuild-work"));
        EXPECT(read(state.runtime) == "fake game\n");
        EXPECT(read(out / "save-sentinel") == "player data" && read(out / "install.prev.log") == failedLog);
        EXPECT(ReadInstallLog(out).runtime == state.runtime && ReadInstallLog(out).state == InstallProgress::State::Done);
        EXPECT(state.log.size() == 3 && state.logLines > state.log.size() && state.log.back() == "finished");
        EXPECT(installer.Snapshot().log.empty() && !installer.FullLog().empty());
    }

    // A second Start while a build runs changes nothing and does not wait for it (it is called on the window's thread; issue #9, S-28).
    {
        const fs::path out = root / "twice";
        TestSetEnv("FAKE_ARKEYAN_BUILDER_PAUSE_STEP", "2");
        EXPECT(installer.Start(kit, "synthetic.iso", out));
        const uint64_t deadline = SDL_GetTicks() + 5000;
        do { state = installer.Snapshot(2); SDL_Delay(5); } while (!(state.steps.size() == 2 && state.done == 1) && installer.Running() && SDL_GetTicks() < deadline);
        const uint32_t pid = installer.ProcessId();
        EXPECT(installer.Running() && pid != 0 && state.state == InstallProgress::State::Running);
        const uint64_t before = SDL_GetTicks();
        EXPECT(!installer.Start(kit, "synthetic.iso", out));
        EXPECT(SDL_GetTicks() - before < 500 && installer.Running() && installer.ProcessId() == pid);
        state = installer.Snapshot(2);
        EXPECT(state.state == InstallProgress::State::Running && !state.startFailed && state.steps.size() == 2);
        installer.Cancel();
        state = wait(installer);
        EXPECT(state.state == InstallProgress::State::Cancelled);
        TestUnsetEnv("FAKE_ARKEYAN_BUILDER_PAUSE_STEP");
    }

    for (const char* ending : { "empty", "exit", "error" })
    {
        TestSetEnv("FAKE_ARKEYAN_BUILDER_ENDING", ending);
        EXPECT(installer.Start(kit, "synthetic.iso", root / ending));
        state = wait(installer);
        EXPECT(state.state == InstallProgress::State::Failed && !state.startFailed && state.runtime.empty());
#if defined(_WIN32) // a background process reports no exit code there (game_install.cpp)
        if (std::string_view(ending) == "exit") EXPECT(state.error.find("stopped before the game was built") != std::string::npos);
#else
        if (std::string_view(ending) == "exit") EXPECT(state.error.find("exit code 7") != std::string::npos);
#endif
        else if (std::string_view(ending) == "error") EXPECT(state.error == "synthetic failure without newline");
        else EXPECT(state.error.find("before the game was built") != std::string::npos);
        EXPECT(!fs::exists(root / ending / "layout.txt"));
    }
    TestUnsetEnv("FAKE_ARKEYAN_BUILDER_ENDING");
    TestUnsetEnv("FAKE_ARKEYAN_BUILDER_DELAY");
    printf("Install lifecycle: ten interruption/failure/retry boundaries, log recovery and startup/EOF errors checked\n");
}
