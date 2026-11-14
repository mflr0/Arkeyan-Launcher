#pragma once

// Running the game and what it leaves behind for support (docs/support.md): the launcher starts the runtime with its output
// in skylauncher-game.log, watches it while the window stays open (GameSession: one game at a time), and reports an unexpected
// exit. Arkeyan.log, written by the runtime itself, sits in the same folder. "Test the portal" runs the runtime's --portal-probe.
#include "launcher/launcher.h"
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

struct SDL_IOStream;
struct SDL_Process;

// The launcher's logs folder: skylauncher-game.log, migration.log (<data root>/logs).
std::filesystem::path GameLogFolder();
// Where the runtime writes Arkeyan.log: the game's folder (<data root>/SSA).
std::filesystem::path RuntimeLogFolder();
inline constexpr char RuntimeLogName[] = "Arkeyan.log";
inline constexpr char RuntimePreviousLogName[] = "Arkeyan.prev.log";
inline constexpr char CaptureLogName[] = "skylauncher-game.log";

struct GameRunResult
{
    bool started = false;
    int exitCode = 0;
    std::string error; // why it didn't start
};

// Starts `runtime` with the disc image and waits until it exits. Its stdout and stderr go to skylauncher-game.log (the
// previous one is kept as skylauncher-game.prev.log). `extraArguments` follow the disc image.
GameRunResult RunGame(
    const std::string& runtime, const std::string& iso, const std::vector<std::string>& extraArguments = {});

// What PLAY starts: the runtime, and the variables it gets on top of the launcher's own environment (each under all its names:
// launcher.h, CreateGameEnvironment).
struct GameLaunch
{
    std::string runtime;
    std::vector<std::pair<std::string, std::string>> variables;
    std::string error; // no runtime: what to tell the player
};
// Puts the game's own video settings and keys into [video] and [input] and saves the settings file, then finds the runtime
// (SKY_LAUNCHER_RUNTIME, else `installed`: the game the launcher built, else the player's own) and the variables: the portal,
// the languages the disc holds, an older build's folders (docs/settings.md).
GameLaunch PrepareGameLaunch(
    const GameInfo& game, LauncherConfig& config, const std::string& configPath, const std::string& installed);
// The language code a game is started in, for its text (firstHeld) or its voices: the one saved, else the launcher's own
// when the disc holds it; one the disc lacks becomes the disc's first (text) or nothing (voices). Empty: the game's default.
std::string GameLanguageToPass(
    std::string saved, const std::vector<std::string>& held, bool firstHeld, std::string_view launcherLanguage);

// The game's exit code when Direct3D 12 showed nothing on this PC (Windows): it wrote gpu_backend=vulkan and asks to be
// started again; GameSession does it once, with SKY_GPU_BACKEND=vulkan (docs/launcher_ui.md, "A game running").
inline constexpr int GameRestartExitCode = 75;

// One game at a time (docs/launcher_ui.md, "A game running"). PLAY starts the game without waiting for it: its output goes
// straight into skylauncher-game.log, so it runs on if the launcher is closed. The game started here is watched until it
// exits (its exit code tells a crash); one this launcher did not start (an earlier launcher's, one started by hand) is seen
// by the marker each game keeps while it runs (save_backup.h, GameRunning), looked at every MarkerMs.
class GameSession
{
public:
    using Marker = std::function<bool(const std::string& game)>; // true while that game's marker says it runs
    static constexpr uint64_t MarkerMs = 1000;
    static constexpr uint64_t StopTimeoutMs = 5000;
    // Native window operations receive only the PID held by this session. Override for headless checks.
    struct WindowControl
    {
        std::function<bool(uint32_t)> available;
        std::function<bool(uint32_t)> show;
        std::function<bool(uint32_t)> close;
    };
    // `games`: the ids whose markers are looked at; `marker`: the game's own by default (GameIsRunning).
    explicit GameSession(std::vector<std::string> games, Marker marker = {}, WindowControl windows = {});
    ~GameSession(); // the game runs on: only the watch ends
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    // Starts `launch.runtime` with the disc image for `game`. False, with `error`, when a game runs already (this one or
    // another, started here or not) or the runtime can't start.
    bool Start(const std::string& game, const GameLaunch& launch, const std::string& iso, std::string& error);
    struct Ended
    {
        std::string game;
        bool ours = false; // started here: its exit code is known
        int exitCode = 0;
        bool stopped = false; // requested here: do not report a forced stop as a crash
        bool restarted =
            false; // it asked to start again with Vulkan (GameRestartExitCode): started again, still running
    };
    // Sees the game's exit, and the markers (`nowMs`: SDL_GetTicks). The game that stopped since the last call, if any.
    std::optional<Ended> Poll(uint64_t nowMs);
    const std::string& Running() const { return m_game; } // the game that runs, "" for none
    bool Ours() const { return m_process != nullptr; }
    uint32_t Pid() const;
    bool Stop(uint64_t nowMs);
    bool Stopping() const { return m_stopping; }
    bool CanShow() const;
    bool Show();

private:
    std::string Marked() const; // the first game whose marker is up
    bool Spawn(const std::string& game, const GameLaunch& launch, const std::string& iso, std::string& error);
    std::vector<std::string> m_games;
    Marker m_marker;
    std::string m_game;
    SDL_Process* m_process = nullptr;
    SDL_IOStream* m_log = nullptr;
    uint64_t m_nextLook = 0;
    void* m_handle = nullptr; // Windows: our own handle on the process, for its exit code
    WindowControl m_windows;
    bool m_stopping = false;
    uint64_t m_forceAt = 0;
    GameLaunch m_launch; // what was started, for the one restart
    std::string m_iso;
    bool m_restarted = false;
};

// The last `maxBytes` of a file, from the start of a line; empty when missing.
std::string ReadLogTail(const std::filesystem::path& file, size_t maxBytes = 256 * 1024);
// A game run's log. The game's own Arkeyan.log and the launcher's capture of what it printed (skylauncher-game.log) hold the
// same lines: a copy carries one of them, the newest, saying which game and when (issue #20: two copies of a run, and no way
// to tell an old run from the one just played).
struct GameLog
{
    std::filesystem::path file; // empty: no log yet
    std::string game;           // its id ("giants"); empty when unknown
    std::time_t written = 0;    // when it was last written
};
// The newest of the capture and the game logs given (each with its game's id); the capture wins within 10 s (the same run,
// with its exit code). The capture names its game on its second line ("# game giants").
GameLog NewestGameLog(
    const std::filesystem::path& capture, const std::vector<std::pair<std::string, std::filesystem::path>>& own);
// The capture and every game's Arkeyan.log in the launcher's data folder.
GameLog LastGameLog();
// "Giants, last written 2026-11-10 09:12:03 (local time), skylauncher-game.log".
std::string GameLogHeading(const GameLog& log);
// What to send for a bug report: the last run's log under its heading (the tail of it).
std::string GameLogText(size_t maxBytes = 384 * 1024);

// One portal found by `<runtime> --portal-probe`: a JSON object per line, then {"done":true,"count":N}.
struct PortalProbeEntry
{
    std::string kind;    // "hid" or "usb"
    std::string vid, pid;
    std::string product;
    bool opened = false;
    std::string status;  // "ok" or "error: ..."
    // What the portal answered to the game's reset (older runtimes: nothing): its id ("01 28"), the model's name, and what
    // Spyro's Adventure makes of it: "works", "warns", "rejected" or "unknown", with the reason.
    std::string id, model, verdict, verdictReason;
    // HID portals: the write method that got an answer (empty: none), and each method tried with what the portal answered
    // ("hid-write", "R: error: write failed (...); S: not tried").
    std::string write;
    std::vector<std::pair<std::string, std::string>> methods;
    // What the device is: its interfaces and drivers ("interface 0 FF/5D/01: 81 interrupt 32 bytes, ...; kernel drivers:
    // 1-3:1.0 xpad", "driver WinUSB"), for Copy.
    std::string details;
};
struct PortalProbeResult
{
    bool ran = false; // the runtime started and printed its final line
    std::string error; // when it didn't
    std::vector<PortalProbeEntry> portals;
    std::vector<std::string>
        notes; // transports that can't be used at all here ("USB portals can't be used: libusb-1.0 not found")
};
// Parses the probe's output (the part tests and the UI share).
PortalProbeResult ParsePortalProbe(const std::string& output);
// Runs the probe, waiting at most `timeoutMs`.
PortalProbeResult RunPortalProbe(const std::string& runtime, uint32_t timeoutMs = 15000);
// Why one portal can't be used, in the launcher's words (translated), with the system's own reason; empty when it answered.
// Several lines for a portal on a driver to remove (steps, then the commands).
std::string PortalStatusText(const PortalProbeEntry& entry);
// The commands that delete the driver package such a portal is on (Windows' pnputil, one per line), for "Copy commands";
// empty when there are none.
std::string PortalDriverCommands(const PortalProbeEntry& entry);
// What the game will do with the portal, given the setting's mode ("auto", "real", "emulated") and the test; `good` is
// whether that is what the player wants. The words never contradict an entry's pill: "Found" answers, "Found, not
// answering" opened without an answer, "Found, can't be opened" did not open.
std::string PortalVerdict(const std::string& mode, const PortalProbeResult& result, bool& good);
// What "Copy" puts on the clipboard for a bug report: every portal with each write method's line, the notes, the verdict.
std::string PortalProbeText(const std::string& mode, const PortalProbeResult& result);
