#pragma once

// "Export a portable copy" (docs/portable.md): the installed game with its disc's files, its keys and the player's figures, as
// one file for the system in use: <name>.app on macOS (a bundle: the files in Contents/Resources/game.skypack, the bundle signed
// again), <name>.exe on Windows and <name>.AppImage on Linux (the pack after the executable, which the system never loads). The
// pack's format is shared/collection/portable_pack.h; the game reads it. No UI: ArkeyanLauncherTest runs it on scratch folders.
#include "launcher/launcher.h"
#include "shared/collection/portable_pack.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

struct PortableSpec
{
    std::string gameId;
    std::filesystem::path runtime;    // the installed game's executable (on macOS, <app>/Contents/MacOS/<name>)
    std::string source;               // the verified disc image, or the copy of the game data
    std::filesystem::path collection; // the player's figures
    std::string keys;                 // the game's [<game>.input] section, as PortableKeys writes it
    portable::Info info;
    std::filesystem::path folder;     // where the portable goes
    std::string name;                 // its file name without the extension
    bool sign = true;                 // macOS: sign the bundle again (a test's stand-in executable can't be)
    int64_t freeBytes = -1;           // the room to assume; < 0: ask the file system (tests)
};

// ".app", ".exe" or ".AppImage": the system this launcher runs on.
const char* PortableExtension();
// The file name for a game: its full name without the characters a file name can't hold ("Skylanders Spyro's Adventure").
std::string PortableName(std::string_view gameId);
// The game's keys as they are now ([<game>.input], seeded from [input] the way PLAY does it), as the text of keys.ini.
std::string PortableKeys(const LauncherConfig& config, std::string_view gameId);
// "20261101-203000", UTC.
std::string PortableStamp();

struct PortableResult
{
    enum class Outcome { Done, NotEnoughSpace, Cancelled, Failed } outcome = Outcome::Failed;
    std::string error;                  // Failed: what went wrong
    uint64_t needed = 0, available = 0; // NotEnoughSpace
    std::filesystem::path path;         // Done: the portable
    uint64_t bytes = 0;                 // Done: its size
};
// Writes the portable into a hidden folder beside it, checks its pack, then puts it in place (an older one of the same name is
// replaced). Nothing is left behind when it fails or is cancelled.
PortableResult ExportPortable(const PortableSpec& spec, const std::atomic<bool>* cancel = nullptr,
    const std::function<void(uint64_t done, uint64_t total)>& progress = {});

// The export on a worker thread.
class PortableExportJob
{
public:
    ~PortableExportJob() { Cancel(); Join(); }
    bool Running() const { return m_started; }
    bool Done() const { return m_done; }
    void Start(const PortableSpec& spec);
    void Cancel() { m_cancel = true; }
    float Fraction() const;
    uint64_t Bytes() const { return m_bytes; }
    uint64_t Total() const { return m_total; }
    PortableResult Collect();

private:
    void Join() { if (m_thread.joinable()) m_thread.join(); }

    std::thread m_thread;
    std::atomic<bool> m_started{ false }, m_done{ false }, m_cancel{ false };
    std::atomic<uint64_t> m_bytes{ 0 }, m_total{ 0 };
    PortableResult m_result;
};

// What to tell the player when an export didn't finish; empty for Done and Cancelled.
std::string PortableFailureText(const PortableResult& result);
