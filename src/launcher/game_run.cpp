#include "launcher/game_run.h"
#include "shared/runtime_names.h"
#include "shared/collection/datadir.h"
#include "launcher/env.h"
#include "launcher/i18n.h"
#include "launcher/game_install.h"
#include "launcher/game_keys.h"
#include "launcher/game_names.h"
#include "launcher/game_settings.h"
#include "launcher/launcher.h"
#include "launcher/updater.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <fstream>
#include "launcher/game_window.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
    std::filesystem::path Path(const std::string& utf8)
    {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }

    std::string Utf8(const std::filesystem::path& path)
    {
        std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    // Starts `arguments` with stdout and stderr piped; false (with SDL's error in `error`) when it can't start.
    SDL_Process* Spawn(const std::vector<std::string>& arguments, std::string& error)
    {
        std::vector<const char*> argv;
        for (const std::string& argument : arguments)
            argv.push_back(argument.c_str());
        argv.push_back(nullptr);
        SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
#if defined(_WIN32)
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true); // no console window
#endif
        SDL_Environment* environment = CreateGameEnvironment(); // both names of every variable: older and newer games
        if (environment != nullptr)
            SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
        SDL_Process* process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (environment != nullptr)
            SDL_DestroyEnvironment(environment);
        if (process == nullptr)
            error = SDL_GetError();
        return process;
    }

    // Reads the process's output until it ends, handing every chunk to `sink`; returns the exit code (-1 if unknown).
    // `timeoutMs` > 0 kills it after that long (the exit code is then -1 and `timedOut` is set).
    template<class Sink>
    int Drain(SDL_Process* process, Sink&& sink, uint32_t timeoutMs, bool* timedOut)
    {
        SDL_IOStream* output = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_STDOUT_POINTER, nullptr));
        int exitCode = -1;
        bool exited = false;
        uint64_t start = SDL_GetTicks(), exitedAt = 0;
#if defined(_WIN32)
        // SDL doesn't report the exit code of a background process: read it from a handle of our own.
        HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(SDL_GetNumberProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_PID_NUMBER, 0)));
#endif
        char buffer[8192];
        for (;;)
        {
            if (!exited && SDL_WaitProcess(process, false, &exitCode))
            {
                exited = true;
                exitedAt = SDL_GetTicks();
            }
            if (exited && SDL_GetTicks() - exitedAt > 1000) // a child that kept the pipe open
                break;
            if (timeoutMs > 0 && !exited && SDL_GetTicks() - start > timeoutMs)
            {
                SDL_KillProcess(process, true);
                if (timedOut != nullptr)
                    *timedOut = true;
                timeoutMs = 0;
            }
            size_t read = output != nullptr ? SDL_ReadIO(output, buffer, sizeof(buffer)) : 0;
            if (read > 0)
            {
                sink(buffer, read);
                continue;
            }
            if (exited && (output == nullptr || SDL_GetIOStatus(output) == SDL_IO_STATUS_EOF || SDL_GetIOStatus(output) == SDL_IO_STATUS_ERROR))
                break;
            SDL_Delay(15);
        }
        if (!exited)
            SDL_WaitProcess(process, true, &exitCode);
#if defined(_WIN32)
        if (handle != nullptr)
        {
            DWORD code = 0;
            if (GetExitCodeProcess(handle, &code) && code != STILL_ACTIVE)
                exitCode = int(code);
            CloseHandle(handle);
        }
#endif
        return exitCode;
    }

    // Minimal reader for the probe's lines: one JSON object, string / number / boolean values, and objects of strings
    // one level deep, whose entries come out as "<key>.<name>" in their order ("methods": each write method's result).
    bool ParseFlatObject(const std::string& line, std::vector<std::pair<std::string, std::string>>& fields)
    {
        size_t i = line.find('{');
        if (i == std::string::npos)
            return false;
        auto skip = [&] { while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++; };
        auto string = [&](std::string& out) {
            if (i >= line.size() || line[i] != '"')
                return false;
            for (i++; i < line.size() && line[i] != '"'; i++)
            {
                if (line[i] == '\\' && i + 1 < line.size())
                {
                    char c = line[++i];
                    out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
                }
                else
                    out += line[i];
            }
            if (i >= line.size())
                return false;
            i++;
            return true;
        };
        i++;
        for (;;)
        {
            skip();
            if (i < line.size() && line[i] == '}')
                return true;
            std::string key, value;
            if (!string(key))
                return false;
            skip();
            if (i >= line.size() || line[i] != ':')
                return false;
            i++;
            skip();
            if (i < line.size() && line[i] == '{')
            {
                for (i++;;)
                {
                    skip();
                    if (i < line.size() && line[i] == '}')
                    {
                        i++;
                        break;
                    }
                    std::string name, entry;
                    if (!string(name))
                        return false;
                    skip();
                    if (i >= line.size() || line[i] != ':')
                        return false;
                    i++;
                    skip();
                    if (!string(entry))
                        return false;
                    fields.emplace_back(key + "." + name, entry);
                    skip();
                    if (i < line.size() && line[i] == ',')
                        i++;
                }
                skip();
                if (i < line.size() && line[i] == ',')
                    i++;
                continue;
            }
            if (i < line.size() && line[i] == '"')
            {
                if (!string(value))
                    return false;
            }
            else
                while (i < line.size() && line[i] != ',' && line[i] != '}')
                    value += line[i++];
            while (!value.empty() && (value.back() == ' ' || value.back() == '\r'))
                value.pop_back();
            fields.emplace_back(key, value);
            skip();
            if (i < line.size() && line[i] == ',')
                i++;
        }
    }
}

std::filesystem::path GameLogFolder()
{
    if (const char* override = LauncherEnv("SKY_LAUNCHER_LOG_DIR"); override != nullptr && *override != 0) // tests
        return Path(override);
    return LauncherDataFolder() / "logs";
}

std::filesystem::path RuntimeLogFolder()
{
    if (const char* override = LauncherEnv("SKY_LAUNCHER_LOG_DIR"); override != nullptr && *override != 0) // tests
        return Path(override);
    return GameInstallFolder({});
}

GameRunResult RunGame(const std::string& runtime, const std::string& iso, const std::vector<std::string>& extraArguments)
{
    GameRunResult result;
    std::filesystem::path folder = GameLogFolder();
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    std::filesystem::path capture = folder / CaptureLogName;
    std::filesystem::rename(capture, folder / "skylauncher-game.prev.log", error);

    // On Linux SDL forks before it finds out that the program is missing: the child then only exits.
    if (!std::filesystem::exists(Path(runtime), error))
    {
        result.error = Tr("Couldn't start the game: the file is missing");
        return result;
    }
    std::vector<std::string> arguments{ runtime, iso };
    arguments.insert(arguments.end(), extraArguments.begin(), extraArguments.end());
    SDL_Process* process = Spawn(arguments, result.error);
    if (process == nullptr)
    {
        result.error = Tf("Couldn't start the game: {}", result.error);
        return result;
    }
    result.started = true;
    std::ofstream log(capture, std::ios::binary | std::ios::trunc);
    log << "# " << runtime << " " << iso << "\n";
    result.exitCode = Drain(process, [&](const char* data, size_t size) { log.write(data, std::streamsize(size)); log.flush(); }, 0, nullptr);
    log << "\n# exit code " << result.exitCode << "\n";
    SDL_DestroyProcess(process);
    return result;
}

std::string GameLanguageToPass(std::string saved, const std::vector<std::string>& held, bool firstHeld, std::string_view launcherLanguage)
{
    if (saved.empty() && std::find(held.begin(), held.end(), launcherLanguage) != held.end())
        saved = std::string(launcherLanguage);
    if (saved.empty() || held.empty() || std::find(held.begin(), held.end(), saved) != held.end())
        return saved;
    return firstHeld ? held.front() : std::string();
}

GameLaunch PrepareGameLaunch(const GameInfo& game, LauncherConfig& config, const std::string& configPath, const std::string& installed)
{
    GameLaunch launch;
    launch.runtime = FindRuntime(game, config, true);
    if (launch.runtime.empty())
        launch.runtime = !installed.empty() ? installed : FindRuntime(game, config);
    if (launch.runtime.empty())
    {
        launch.error = Tf("The game ({}) wasn't found next to Arkeyan Launcher.\n\nPut it in the same folder.", game.runtime);
        return launch;
    }
    // The video settings of this game are what the runtime reads from [video] (game_settings.h), and its keys from [input].
    ApplyGameVideo(config, game.id);
    ApplyGameInput(config, game.id);
    if (!SaveLauncherConfig(configPath, config))
    {
        launch.runtime.clear();
        launch.error = Tr("Couldn't save the settings");
        return launch;
    }
    // Settings the runtime already takes from its environment (the rest it reads from launcher.ini; docs/settings.md).
    auto setting = [&](const char* section, const char* key) {
        auto entry = config.find(section);
        if (entry == config.end())
            return std::string();
        auto value = entry->second.find(key);
        return value != entry->second.end() ? value->second : std::string();
    };
    auto pass = [&](const char* variable, const char* section, const char* key) {
        if (std::string value = setting(section, key); !value.empty())
            launch.variables.emplace_back(variable, value);
    };
    pass("SKY_PORTAL", "portal", "mode");
    // [portal] place is the older list of figures to start on the portal; once the seats moved to the session file
    // ([collection] moved) a left-over one would put its figures back over the seats the player chose.
    if (setting("collection", "moved") != "1")
        pass("SKY_PORTAL_PLACE", "portal", "place");
    // A language the disc does not hold is not passed as is: the text takes the disc's first language and the voices
    // follow the text, which is what the Language tab shows then. Nothing known about the disc: passed as saved.
    // Nothing chosen: the launcher's own language when the disc holds it (the Language tab shows that too), else the game's.
    const art::DiscLanguages onDisc = StoredDiscLanguages(config, game.id);
    auto passLanguage = [&](const char* variable, const char* key, const std::vector<std::string>& held, bool firstHeld) {
        if (std::string value = GameLanguageToPass(setting(game.id, key), held, firstHeld, i18n::Current()); !value.empty())
            launch.variables.emplace_back(variable, value);
    };
    passLanguage("SKY_LANGUAGE", "language", onDisc.text, true);
    passLanguage("SKY_VOICE_LANGUAGE", "voice_language", onDisc.voice, false);
    // A game built before the one data folder would look for its saves and settings in the older folders, which the
    // migration emptied: point it at the new ones until "Update the game" rebuilds it (docs/install.md, "Older builds").
    const std::filesystem::path gameFolder = GameInstallFolder({});
    if (IsOlderLayoutBuild(Path(launch.runtime), gameFolder))
    {
        launch.variables.emplace_back("SKY_LAUNCHER_INI", configPath);
        launch.variables.emplace_back("SKY_SAVE_DIR", Utf8(gameFolder / "saves"));
        launch.variables.emplace_back("SKY_LOG", Utf8(gameFolder / RuntimeLogName));
        launch.variables.emplace_back("SKY_PORTRAITS", Utf8(gameFolder / "portraits"));
    }
    return launch;
}

GameSession::GameSession(std::vector<std::string> games, Marker marker, WindowControl windows)
    : m_games(std::move(games)), m_marker(std::move(marker)), m_windows(std::move(windows))
{
    if (!m_marker)
        m_marker = [](const std::string& game) { return GameIsRunning(FoldersOf(game)); };
    if (!m_windows.available) m_windows.available = game_window::Available;
    if (!m_windows.show) m_windows.show = game_window::Show;
#if defined(_WIN32)
    if (!m_windows.close) m_windows.close = game_window::Close;
#endif
}

GameSession::~GameSession()
{
    if (m_log != nullptr)
        SDL_CloseIO(m_log);
    if (m_process != nullptr)
        SDL_DestroyProcess(m_process); // the game itself is not stopped
#if defined(_WIN32)
    if (m_handle != nullptr)
        CloseHandle(HANDLE(m_handle));
#endif
}

std::string GameSession::Marked() const
{
    for (const std::string& game : m_games)
        if (m_marker(game))
            return game;
    return {};
}

bool GameSession::Start(const std::string& game, const GameLaunch& launch, const std::string& iso, std::string& error)
{
    // A game started by hand a moment ago counts too: the markers are looked at now, not at the next poll.
    if (m_game.empty())
        m_game = Marked();
    if (!m_game.empty())
    {
        error = Tf("{} is running", GameName(m_game, GameNameForm::Short));
        return false;
    }
    std::error_code ignored;
    if (launch.runtime.empty() || !std::filesystem::exists(Path(launch.runtime), ignored))
    {
        error = Tr("Couldn't start the game: the file is missing");
        return false;
    }
    // The output goes straight into the file (the previous run's kept as .prev): nothing of the launcher stands between the
    // game and its log, and a launcher that is closed leaves the game running.
    const std::filesystem::path folder = GameLogFolder(), capture = folder / CaptureLogName;
    std::filesystem::create_directories(folder, ignored);
    std::filesystem::rename(capture, folder / "skylauncher-game.prev.log", ignored);
    SDL_IOStream* log = SDL_IOFromFile(Utf8(capture).c_str(), "ab");
    if (log != nullptr)
    {
        const std::string head = fmt::format("# {} {}\n# game {}\n", launch.runtime, iso, game);
        SDL_WriteIO(log, head.data(), head.size());
        SDL_FlushIO(log);
    }
    std::vector<const char*> argv{ launch.runtime.c_str(), iso.c_str(), nullptr };
    SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argv.data());
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    if (log != nullptr)
    {
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_REDIRECT);
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_POINTER, log);
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    }
    else
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
#if defined(_WIN32)
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true); // no console window
#endif
    SDL_Environment* environment = CreateGameEnvironment(launch.variables);
    if (environment != nullptr)
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, environment);
    SDL_Process* process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (environment != nullptr)
        SDL_DestroyEnvironment(environment);
    if (process == nullptr)
    {
        error = Tf("Couldn't start the game: {}", SDL_GetError());
        if (log != nullptr)
            SDL_CloseIO(log);
        return false;
    }
    m_process = process;
    m_log = log;
    m_game = game;
    m_stopping = false;
    m_forceAt = 0;
#if defined(_WIN32)
    // SDL doesn't report the exit code of a background process: read it from a handle of our own.
    m_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, DWORD(SDL_GetNumberProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_PID_NUMBER, 0)));
#endif
    fprintf(stderr, "launcher: %s started (process %lld)\n", game.c_str(), (long long)SDL_GetNumberProperty(SDL_GetProcessProperties(process), SDL_PROP_PROCESS_PID_NUMBER, 0));
    return true;
}

uint32_t GameSession::Pid() const
{
    return m_process == nullptr ? 0 : uint32_t(SDL_GetNumberProperty(SDL_GetProcessProperties(m_process), SDL_PROP_PROCESS_PID_NUMBER, 0));
}

bool GameSession::Stop(uint64_t nowMs)
{
    if (!Ours() || Pid() == 0) return false;
    if (m_stopping) return true; // repeated presses never postpone the timeout
    if (m_windows.close)
        m_windows.close(Pid()); // WM_CLOSE; a missing or unresponsive window still gets the timeout
    else
        SDL_KillProcess(m_process, false); // SIGTERM: SDL turns it into the same quit event as closing the window
    m_stopping = true;
    m_forceAt = nowMs + StopTimeoutMs;
    return true;
}

bool GameSession::CanShow() const
{
    return Ours() && Pid() != 0 && !m_stopping && m_windows.available(Pid());
}

bool GameSession::Show()
{
    return CanShow() && m_windows.show(Pid());
}

std::optional<GameSession::Ended> GameSession::Poll(uint64_t nowMs)
{
    if (m_process != nullptr)
    {
        int exitCode = -1;
        if (!SDL_WaitProcess(m_process, false, &exitCode))
        {
            if (m_stopping && nowMs >= m_forceAt)
            {
                SDL_KillProcess(m_process, true); // this SDL process owns exactly the PID started above
                m_forceAt = nowMs + StopTimeoutMs; // retry a failed termination without blocking the window
            }
            return std::nullopt;
        }
#if defined(_WIN32)
        if (m_handle != nullptr)
        {
            DWORD code = 0;
            if (GetExitCodeProcess(HANDLE(m_handle), &code) && code != STILL_ACTIVE)
                exitCode = int(code);
            CloseHandle(HANDLE(m_handle));
            m_handle = nullptr;
        }
#endif
        if (m_log != nullptr)
        {
            const std::string tail = fmt::format("\n# exit code {}\n", exitCode);
            SDL_WriteIO(m_log, tail.data(), tail.size());
            SDL_CloseIO(m_log);
            m_log = nullptr;
        }
        SDL_DestroyProcess(m_process);
        m_process = nullptr;
        m_nextLook = 0; // and the markers again at once
        fprintf(stderr, "launcher: %s stopped (exit code %d)\n", m_game.c_str(), exitCode);
        return Ended{ std::exchange(m_game, std::string()), true, exitCode, std::exchange(m_stopping, false) };
    }
    if (nowMs < m_nextLook)
        return std::nullopt;
    m_nextLook = nowMs + MarkerMs;
    std::string marked = Marked();
    if (marked == m_game)
        return std::nullopt;
    std::string before = std::exchange(m_game, std::move(marked));
    if (before.empty())
        return std::nullopt; // one found running
    return Ended{ std::move(before), false, 0 };
}

std::string ReadLogTail(const std::filesystem::path& file, size_t maxBytes)
{
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream)
        return {};
    size_t size = size_t(stream.tellg());
    size_t start = size > maxBytes ? size - maxBytes : 0;
    stream.seekg(std::streamoff(start));
    std::string text(size - start, '\0');
    stream.read(text.data(), std::streamsize(text.size()));
    if (start > 0)
        if (size_t newline = text.find('\n'); newline != std::string::npos)
            text.erase(0, newline + 1);
    return text;
}

namespace
{
    std::time_t WrittenAt(const std::filesystem::path& file)
    {
        SDL_PathInfo info{};
        if (!SDL_GetPathInfo(Utf8(file).c_str(), &info) || info.type != SDL_PATHTYPE_FILE)
            return 0;
        return std::time_t(info.modify_time / 1000000000);
    }

    // "# game <id>" in the capture's first lines (GameSession::Start writes it).
    std::string CaptureGame(const std::filesystem::path& capture)
    {
        std::ifstream in(capture, std::ios::binary);
        std::string line;
        for (int i = 0; i < 3 && std::getline(in, line); i++)
            if (line.starts_with("# game "))
                return line.substr(7);
        return {};
    }
}

GameLog NewestGameLog(const std::filesystem::path& capture, const std::vector<std::pair<std::string, std::filesystem::path>>& own)
{
    GameLog newest;
    for (const auto& [game, file] : own)
        if (std::time_t written = WrittenAt(file); written != 0 && written > newest.written)
            newest = { file, game, written };
    if (std::time_t written = WrittenAt(capture); written != 0 && written + 10 >= newest.written)
    {
        std::string game = CaptureGame(capture);
        if (game.empty() && !newest.file.empty() && newest.written <= written + 10 && written <= newest.written + 10)
            game = newest.game; // an older launcher's capture of the same run
        newest = { capture, game, written };
    }
    return newest;
}

GameLog LastGameLog()
{
    std::vector<std::pair<std::string, std::filesystem::path>> own;
    if (const char* override = LauncherEnv("SKY_LAUNCHER_LOG_DIR"); override != nullptr && *override != 0) // tests
        own.emplace_back(std::string(ActiveGame()), runtime_names::ReadLog(Path(override)));
    else
        for (const GameNames& names : g_gameNames)
            own.emplace_back(names.id, runtime_names::ReadLog(LauncherDataFolder() / datadir::GameFolderName(names.id)));
    return NewestGameLog(GameLogFolder() / CaptureLogName, own);
}

std::string GameLogHeading(const GameLog& log)
{
    char when[32] = "unknown time";
    std::tm local{};
#if defined(_WIN32)
    const bool known = localtime_s(&local, &log.written) == 0;
#else
    const bool known = localtime_r(&log.written, &local) != nullptr;
#endif
    if (known)
        std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &local);
    const char* name = GameName(log.game, GameNameForm::Short);
    return fmt::format("{}, last written {} (local time), {}", name != nullptr ? name : "unknown game", when, log.file.filename().string());
}

std::string GameLogText(size_t maxBytes)
{
    const GameLog log = LastGameLog();
    std::string part = log.file.empty() ? std::string() : ReadLogTail(log.file, maxBytes);
    if (part.empty())
        return {};
    return fmt::format("===== Game log: {} =====\n{}\n", GameLogHeading(log), part);
}

PortalProbeResult ParsePortalProbe(const std::string& output)
{
    PortalProbeResult result;
    size_t begin = 0;
    while (begin <= output.size())
    {
        size_t end = output.find('\n', begin);
        if (end == std::string::npos)
            end = output.size();
        std::vector<std::pair<std::string, std::string>> fields;
        if (ParseFlatObject(output.substr(begin, end - begin), fields))
        {
            auto get = [&](const char* key) {
                for (const auto& [name, value] : fields)
                    if (name == key)
                        return value;
                return std::string();
            };
            if (!get("transport").empty() && get("available") == "false")
            {
                std::string why = get("status");
                if (why.starts_with("error: "))
                    why.erase(0, 7);
                result.notes.push_back(Tf("{} can't be used: {}", get("transport") == "usb" ? Tr("USB (Xbox) portals") : Tr("HID portals"), why));
            }
            else if (get("done") == "true")
                result.ran = true;
            else if (!get("kind").empty())
            {
                PortalProbeEntry entry{ get("kind"), get("vid"), get("pid"), get("product"), get("opened") == "true", get("status") };
                entry.id = get("id");
                entry.model = get("model");
                entry.verdict = get("verdict");
                entry.verdictReason = get("verdict_reason");
                entry.write = get("write");
                for (const auto& [name, value] : fields)
                    if (name.starts_with("methods."))
                        entry.methods.emplace_back(name.substr(8), value);
                if (entry.product.empty())
                    entry.product = get("name"); // the first draft of the contract
                if (entry.status.empty())
                    entry.status = entry.opened ? "ok" : "error: not opened";
                result.portals.push_back(std::move(entry));
            }
        }
        begin = end + 1;
    }
    if (!result.ran)
        result.error = Tr("The game didn't finish the portal test.");
    return result;
}

std::string PortalStatusText(const PortalProbeEntry& entry)
{
    if (entry.status.empty() || entry.status == "ok")
        return {};
    std::string why = entry.status.starts_with("error: ") ? entry.status.substr(7) : entry.status;
    // "<what> (<the system's reason>)": the reason stays as the system gave it.
    auto inside = [&](const char* what) -> std::optional<std::string> {
        if (!why.starts_with(what))
            return std::nullopt;
        size_t open = why.find('(', strlen(what)), close = why.rfind(')');
        return open != std::string::npos && close != std::string::npos && close > open ? why.substr(open + 1, close - open - 1) : std::string();
    };
    if (why.find("udev rule") != std::string::npos)
        return Tr("Linux needs the portal's udev rule: see the README.");
    if (why.find("answer within 1 s") != std::string::npos)
        return Tr("The portal didn't answer the game within 1 second.");
    if (auto reason = inside("write failed"))
        return Tf("The portal refused the game's command: {}.", *reason);
    if (auto reason = inside("read failed"))
        return Tf("Reading the portal failed: {}.", *reason);
    if (auto reason = inside("cannot open"))
        return Tf("The portal can't be opened: {}.", *reason);
    if (auto reason = inside("cannot claim interface"))
        return Tf("Another program is using the portal: {}.", *reason);
    if (why.starts_with("no portal interface"))
        return Tr("This device has no portal interface.");
    return why;
}

std::string PortalVerdict(const std::string& mode, const PortalProbeResult& result, bool& good)
{
    // Opened is not enough: a portal that opens but does not answer the game's commands (its status an error) is no
    // portal for the game. Older runtimes give no status.
    bool answered = false, opened = false;
    for (const PortalProbeEntry& entry : result.portals)
    {
        opened = opened || entry.opened;
        answered = answered || (entry.opened && (entry.status.empty() || entry.status == "ok"));
    }
    good = answered || mode == "emulated";
    if (mode == "emulated")
        return Tr("Virtual portal chosen.");
    if (answered)
        return Tr("Real portal: the game will use it.");
    if (result.portals.empty())
        return mode == "real" ? Tr("No portal found: the game will wait for one.") : Tr("No portal found: using the virtual portal.");
    if (opened)
        return mode == "real" ? Tr("The portal doesn't answer: the game will wait.") : Tr("The portal doesn't answer: using the virtual portal.");
    return mode == "real" ? Tr("The portal can't be opened: the game will wait.") : Tr("The portal can't be opened: using the virtual portal.");
}

std::string PortalProbeText(const std::string& mode, const PortalProbeResult& result)
{
    std::string text = fmt::format("Portal test (Arkeyan Launcher {}, portal mode {})\n", LauncherVersion(), mode);
    if (!result.error.empty())
        return text + result.error + "\n";
    if (result.portals.empty())
        text += "no portal found\n";
    for (const PortalProbeEntry& entry : result.portals)
    {
        text += fmt::format("{} {}:{} \"{}\": {}, {}\n", entry.kind, entry.vid, entry.pid, entry.product, entry.opened ? "opened" : "not opened", entry.status);
        if (!entry.id.empty())
            text += fmt::format("  model: {} {}; Spyro's Adventure: {} ({})\n", entry.id, entry.model, entry.verdict, entry.verdictReason);
        for (const auto& [method, line] : entry.methods)
            text += fmt::format("  {}: {}\n", method, line);
        if (!entry.write.empty())
            text += fmt::format("  the game writes with {}\n", entry.write);
    }
    for (const std::string& note : result.notes)
        text += note + "\n";
    bool good = false;
    return text + "=> " + PortalVerdict(mode, result, good) + "\n";
}

PortalProbeResult RunPortalProbe(const std::string& runtime, uint32_t timeoutMs)
{
    PortalProbeResult result;
    std::string error;
    SDL_Process* process = Spawn({ runtime, "--portal-probe" }, error);
    if (process == nullptr)
    {
        result.error = Tf("Couldn't start the game: {}", error);
        return result;
    }
    std::string output;
    bool timedOut = false;
    int code = Drain(process, [&](const char* data, size_t size) { output.append(data, size); }, timeoutMs, &timedOut);
    SDL_DestroyProcess(process);
    result = ParsePortalProbe(output);
    if (!result.ran)
        result.error = timedOut ? Tr("The portal test took too long and was stopped.")
            : code == 2 ? Tr("This version of the game doesn't have the portal test yet. Update the game.")
                        : Tr("The game didn't finish the portal test.");
    return result;
}
