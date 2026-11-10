#pragma once

// "Find my edition" (docs/launcher_ui.md): on a disc of an edition the launcher doesn't know, the kit's Arkeyan-Builder
// --edition-report reads it and writes a short text for a "New disc edition" issue: the disc's ids, region and languages,
// the hashes of its executable and of its loaded game code, and which supported release has the same code. No path, no
// name, no byte of the game.
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

struct EditionReport
{
    bool ok = false;
    std::string text; // the report, or why there is none (Arkeyan-Builder's @error, in English)
};

// Runs `builder --edition-report <image>` and waits for it (a few seconds: it reads default.xex and the disc's file list).
EditionReport RunEditionReport(const std::filesystem::path& builder, const std::string& image);

// The same on a worker thread, for the game page.
class EditionReportJob
{
public:
    ~EditionReportJob();
    void Start(const std::filesystem::path& builder, const std::string& image);
    bool Running() const;
    std::optional<EditionReport> Take(); // the finished report, once

private:
    std::thread m_thread;
    mutable std::mutex m_mutex;
    bool m_running = false;
    std::optional<EditionReport> m_result;
};
