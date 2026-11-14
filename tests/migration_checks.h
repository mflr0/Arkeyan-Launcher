#pragma once

inline void FolderMigrationChecks(const std::filesystem::path& scratch)
{
    namespace fs = std::filesystem;
    auto put = [](const fs::path& file, const std::string& text) {
        fs::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << text;
    };
    auto read = [](const fs::path& file) {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    for (bool copy : { false, true })
    {
        const fs::path base = scratch / "folder-migration" / (copy ? "copy" : "rename");
        datadir::OldRoots old{ base / "old-game", base / "old-launcher", base / "old-local" };
        const fs::path root = base / "new";
        const fs::path build = old.skylauncher / "games" / "aaa";
        put(old.ssarecomp / "SSARecomp" / "saves" / "slot", "synthetic save");
        put(old.ssarecomp / "figures" / "test.bin", "synthetic figure");
        put(old.ssarecomp / "xbox-signing" / "keep", "development file");
        put(build / "SSARecomp", "synthetic executable");
        put(old.skylauncher / "games" / "bbb" / "keep", "second build");
        put(old.skylauncher / "kit" / "v1.0.0" / "builder", "synthetic kit");
        put(old.localSsarecomp / "toolchains" / "compiler" / "ready", "synthetic compiler");
        put(old.ssarecomp / "launcher.ini",
            "[install.synthetic]\nruntime=" + datadir::Text(build / "SSARecomp") + "\nfuture=keep\n");
        datadir::MigrationOptions options{ copy, "test" };
        const auto report = datadir::Migrate(old, root, options);
        EXPECT(report.failed == 0 && report.rewritten == 1 && (copy ? report.copied > 0 : report.moved > 0));
        EXPECT(read(root / "SSA" / "saves" / "slot") == "synthetic save" &&
               read(root / "SSA" / "figures" / "test.bin") == "synthetic figure");
        EXPECT(read(root / "SSA" / "SSARecomp") == "synthetic executable" && !fs::exists(build));
        EXPECT(read(old.skylauncher / "games" / "bbb" / "keep") == "second build");
        EXPECT(read(old.ssarecomp / "xbox-signing" / "keep") == "development file");
        EXPECT(read(root / "kit" / "v1.0.0" / "builder") == "synthetic kit" &&
               read(root / "toolchains" / "compiler" / "ready") == "synthetic compiler");
        auto config = LoadLauncherConfig(datadir::Text(root / "launcher.ini"));
        EXPECT(config["install.synthetic"]["runtime"] == datadir::Text(root / "SSA" / "SSARecomp") &&
               config["install.synthetic"]["future"] == "keep");
        EXPECT(fs::exists(root / "logs" / "migration.log"));
        // Resume the save migration independently of the builds retained in the old launcher folder.
        datadir::OldRoots gameOnly{ old.ssarecomp, {}, {} };
        EXPECT(!datadir::Migrate(gameOnly, root, options).Changed());
        put(old.ssarecomp / "SSARecomp" / "saves" / "slot", "synthetic save");
        const auto duplicate = datadir::Migrate(gameOnly, root, options);
        EXPECT(duplicate.dropped == 1 && duplicate.failed == 0 && !fs::exists(old.ssarecomp / "SSARecomp"));
        // A same-size conflict is preserved beside the current save, including a preexisting suffix.
        put(root / "SSA" / "saves.old-test" / "slot", "earlier conflict");
        put(old.ssarecomp / "SSARecomp" / "saves" / "slot", "different save");
        const auto conflict = datadir::Migrate(gameOnly, root, options);
        EXPECT(conflict.kept == 1 && conflict.failed == 0);
        EXPECT(read(root / "SSA" / "saves" / "slot") == "synthetic save" &&
               read(root / "SSA" / "saves.old-test-2" / "slot") == "different save");
        EXPECT(read(root / "SSA" / "saves.old-test" / "slot") == "earlier conflict");

        // An unwritable destination shape retains the source; removing the obstacle allows a later pass to finish.
        const fs::path blocked = base / "blocked";
        put(blocked / "SSA", "destination obstacle");
        put(old.ssarecomp / "SSARecomp" / "saves" / "slot", "retry save");
        const auto failed = datadir::Migrate(gameOnly, blocked, options);
        EXPECT(failed.failed == 1 && read(old.ssarecomp / "SSARecomp" / "saves" / "slot") == "retry save");
        EXPECT(read(blocked / "logs" / "migration.log").find("FAILED") != std::string::npos);
        fs::remove(blocked / "SSA");
        EXPECT(datadir::Migrate(gameOnly, blocked, options).failed == 0 &&
               read(blocked / "SSA" / "saves" / "slot") == "retry save");
    }
    printf("Folder migration: rename/copy, path rewrite, duplicate recovery, conflicts and failed-destination retry "
           "checked\n");
}
