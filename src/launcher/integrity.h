#pragma once

// The file check (docs/install.md, "File check"): at every start, on a thread, the kit in use and every installed game
// are compared with their file lists (shared/collection/file_list.h). The kit carries its own (files.sha256, written by the
// kit's build); a game's is written by the launcher when its install ends. An antivirus that took or "cleaned" one of
// these files is then named to the player, with a Repair, instead of a build that fails or a game that won't start.
#include "shared/collection/file_list.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// What a folder to check is: the kit (id "kit") or a game ("ssa", "giants", ...).
struct IntegrityTarget
{
    std::string id;
    std::filesystem::path folder;
};

// A folder with missing or changed files.
struct IntegrityFinding
{
    std::string id;
    std::filesystem::path folder;
    std::vector<filelist::Problem> problems;
};

// The files an install wrote into `gameFolder` for the game `runtime` (Arkeyan-Builder's @done path): the runtime, or
// its whole .app on macOS, and next to it the fonts, the notices, game_icon.* and game.desktop. Relative, '/' separated.
// Empty when the runtime is not inside the folder.
std::vector<std::string> InstalledGameFiles(
    const std::filesystem::path& gameFolder, const std::filesystem::path& runtime);

// Writes the game folder's files.sha256 when an install ends; false (and `error`) when it can't.
bool WriteInstalledGameList(
    const std::filesystem::path& gameFolder, const std::filesystem::path& runtime, std::string& error);

// Checks one folder now (the kit before an install starts); empty `problems` when it is fine or has no list.
IntegrityFinding CheckFolder(const IntegrityTarget& target, const std::atomic<bool>* cancel = nullptr);

// The check at start, on a thread: the window opens at once and polls Snapshot().
class IntegrityCheck
{
public:
    struct Report
    {
        bool done = false;
        std::vector<IntegrityFinding> findings; // only the folders with a problem
        size_t files = 0, hashed = 0;           // listed, and read in full (the rest matched their last check)
        uint64_t hashedBytes = 0;
        double seconds = 0;
    };
    ~IntegrityCheck();
    void Start(std::vector<IntegrityTarget> targets); // after the list being written, if any
    bool Running() const { return m_running; }
    Report Snapshot();
    // WriteInstalledGameList on a thread (hashing a game takes about half a second): the window goes on meanwhile.
    void WriteGameList(const std::filesystem::path& gameFolder, const std::filesystem::path& runtime);
    bool WritingList() const { return m_writing; }

private:
    std::mutex m_mutex;
    Report m_report;
    std::thread m_thread, m_writer;
    std::atomic<bool> m_running{ false }, m_cancel{ false }, m_writing{ false };
};
