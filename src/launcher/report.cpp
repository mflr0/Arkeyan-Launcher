#include "shared/runtime_names.h"
#include "launcher/report.h"
#include "launcher/game_install.h"
#include "launcher/game_run.h"
#include "shared/collection/datadir.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
    std::string Lower(std::string text)
    {
        for (char& c : text)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    // Alphanumeric or underscore: a name may not start or end inside one of these.
    bool WordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

    bool MatchesAt(const std::string& text, size_t at, const std::string& needle)
    {
        if (at + needle.size() > text.size())
            return false;
        for (size_t i = 0; i < needle.size(); i++)
            if (std::tolower(static_cast<unsigned char>(text[at + i])) !=
                std::tolower(static_cast<unsigned char>(needle[i])))
                return false;
        return true;
    }

    std::string ReadFile(const fs::path& file, size_t maxBytes = SIZE_MAX)
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
            return {};
        std::string text;
        if (maxBytes == SIZE_MAX)
        {
            std::ostringstream buffer;
            buffer << stream.rdbuf();
            return buffer.str();
        }
        text.resize(maxBytes);
        stream.read(text.data(), std::streamsize(text.size()));
        text.resize(size_t(stream.gcount()));
        return text;
    }

    // The first line that starts with `prefix`, without the line's ending.
    std::string FindLogLine(const std::string& log, const char* prefix)
    {
        size_t begin = 0;
        while (begin < log.size())
        {
            size_t end = log.find('\n', begin);
            if (end == std::string::npos)
                end = log.size();
            std::string line = log.substr(begin, end - begin);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.starts_with(prefix))
                return line.substr(std::strlen(prefix));
            begin = end + 1;
        }
        return {};
    }

    std::string Utf8(const std::wstring& text)
    {
#if defined(_WIN32)
        if (text.empty())
            return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
        std::string out(size_t(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
        return out;
#else
        return std::string(text.begin(), text.end());
#endif
    }

    std::string HostName()
    {
#if defined(_WIN32)
        wchar_t buffer[256] = {};
        DWORD size = DWORD(std::size(buffer));
        if (GetComputerNameW(buffer, &size))
            return Utf8(std::wstring(buffer, size));
        return {};
#else
        char buffer[256] = {};
        if (gethostname(buffer, sizeof(buffer) - 1) == 0)
            return buffer;
        return {};
#endif
    }

    std::string UserName()
    {
#if defined(_WIN32)
        wchar_t buffer[256] = {};
        DWORD size = DWORD(std::size(buffer));
        if (GetUserNameW(buffer, &size))
            return Utf8(std::wstring(buffer, size > 0 ? size - 1 : 0)); // size counts the ending 0
        return {};
#else
        if (const passwd* entry = getpwuid(getuid()); entry != nullptr && entry->pw_name != nullptr)
            return entry->pw_name;
        return {};
#endif
    }

    std::string HomeFolder()
    {
#if defined(_WIN32)
        if (const wchar_t* profile = _wgetenv(L"USERPROFILE"); profile != nullptr && *profile != L'\0')
            return Utf8(profile);
        return {};
#else
        if (const passwd* entry = getpwuid(getuid()); entry != nullptr && entry->pw_dir != nullptr)
            return entry->pw_dir;
        if (const char* home = std::getenv("HOME"); home != nullptr)
            return home;
        return {};
#endif
    }

    // --- Redaction rules ------------------------------------------------------------------------------------------

    struct Rule
    {
        std::string needle;
        std::string replacement;
    };

    void AddRule(std::vector<Rule>& rules, const std::string& needle, const std::string& replacement)
    {
        if (needle.empty())
            return;
        for (const Rule& rule : rules)
            if (rule.needle == needle)
                return;
        rules.push_back({ needle, replacement });
    }

    // The path with its two separator spellings, without a trailing separator.
    void AddPathRules(std::vector<Rule>& rules, std::string path, const std::string& replacement)
    {
        while (path.size() > 1 && (path.back() == '/' || path.back() == '\\'))
            path.pop_back();
        if (path.empty() || path == "/" || path == "\\")
            return;
        std::string forward = path, back = path;
        std::replace(forward.begin(), forward.end(), '\\', '/');
        std::replace(back.begin(), back.end(), '/', '\\');
        AddRule(rules, path, replacement);
        AddRule(rules, forward, replacement);
        AddRule(rules, back, replacement);
    }

    // Where a path to a removable or second drive starts at `at`, the length of that root; 0: none does.
    size_t DriveRoot(const std::string& line, size_t at)
    {
        const char before = at > 0 ? line[at - 1] : ' ';
        const bool word = (before >= '0' && before <= '9') || (before >= 'A' && before <= 'Z') ||
                          (before >= 'a' && before <= 'z') || static_cast<unsigned char>(before) >= 0x80;
        if (word || before == '~' || before == '>' || before == '.' || before == '_' || before == '-')
            return 0;
        for (const std::string root : { "/Volumes/", "/media/", "/run/media/", "/var/run/media/", "/mnt/" })
            if (line.compare(at, root.size(), root) == 0)
                return root.size();
        if (at + 2 < line.size() && line[at + 1] == ':' && (line[at + 2] == '/' || line[at + 2] == '\\'))
        {
            // C: is the system's: its folders of the player's are the home folder's and launcher.ini's rules.
            const char letter = line[at];
            if (((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z')) && letter != 'C' &&
                letter != 'c')
                return 3;
        }
        if (before != ':' && before != '\\' && at + 2 < line.size() && line.compare(at, 2, "\\\\") == 0 &&
            line[at + 2] != '\\')
            return 2; // a network share
        return 0;
    }

    // Whether what follows a path's last separator starts with a file's name (kept: which disc, which log).
    bool NamesFile(std::string text)
    {
        for (char& c : text)
            c = c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
        for (const std::string extension : { ".iso", ".log", ".ini", ".app", ".exe", ".appimage", ".json", ".sky",
                 ".bin", ".dump", ".xex", ".dds", ".png", ".txt", ".zip", ".dll", ".dylib", ".so" })
            for (size_t at = text.find(extension); at != std::string::npos; at = text.find(extension, at + 1))
            {
                const size_t after = at + extension.size();
                if (after == text.size() || std::string(" ,;:)]'").find(text[after]) != std::string::npos)
                    return true;
            }
        return false;
    }

    // Every path on an external drive becomes "<drive>/<its file>". Folder names hold spaces, so a path runs to the end of
    // its line or quote: a line can lose more than the path, never less.
    std::string HideDrives(const std::string& text)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t at = 0; at < text.size();)
        {
            const size_t root = DriveRoot(text, at);
            if (root == 0)
            {
                out += text[at++];
                continue;
            }
            size_t end = text.find_first_of("\r\n\t\"|", at + root);
            if (end == std::string::npos)
                end = text.size();
            const std::string path = text.substr(at + root, end - at - root);
            const size_t slash = path.find_last_of("/\\");
            const std::string last = slash == std::string::npos ? std::string() : path.substr(slash + 1);
            out += "<drive>";
            if (NamesFile(last))
                out += path[slash] + last;
            at = end;
        }
        return out;
    }

    std::string Sanitise(std::string text)
    {
        // The report ends with a newline, and no NUL byte survives a text editor.
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        return text;
    }

    void Section(std::string& report, const std::string& title, const fs::path& file)
    {
        report += "===== " + title + " =====\n";
        std::string text = ReadFile(file);
        report += text.empty() ? "(empty or not found)\n" : text;
        if (!report.empty() && report.back() != '\n')
            report += '\n';
        report += '\n';
    }

    std::string Stamp()
    {
        std::time_t now = std::time(nullptr);
        std::tm parts{};
#if defined(_WIN32)
        localtime_s(&parts, &now);
#else
        localtime_r(&now, &parts);
#endif
        char text[32] = {};
        std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &parts);
        return text;
    }

    std::string FileStamp()
    {
        std::time_t now = std::time(nullptr);
        std::tm parts{};
#if defined(_WIN32)
        localtime_s(&parts, &now);
#else
        localtime_r(&now, &parts);
#endif
        char text[32] = {};
        std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &parts);
        return text;
    }
}

std::string launcher_report::Redact(const std::string& text, const Identity& identity)
{
    std::vector<Rule> rules;
    // The disc image's folder first: it often sits inside the home folder.
    if (!identity.isoFolder.empty())
        AddPathRules(rules, identity.isoFolder, "<iso folder>");
    if (!identity.gamesFolder.empty()) // a drive's name often is the player's
        AddPathRules(rules, identity.gamesFolder, "<games folder>");
    for (const std::string& folder : identity.otherFolders)
        AddPathRules(rules, folder, "<folder>");

    if (!identity.homeFolder.empty())
        AddPathRules(rules, identity.homeFolder, "~");
    if (!identity.userName.empty())
    {
        AddPathRules(rules, "/Users/" + identity.userName, "~");
        AddPathRules(rules, "/home/" + identity.userName, "~");
        AddPathRules(rules, "C:\\Users\\" + identity.userName, "~");
        AddPathRules(rules, "C:/Users/" + identity.userName, "~");
    }
    if (!identity.computerName.empty())
    {
        AddRule(rules, identity.computerName, "<computer>");
        if (size_t dot = identity.computerName.find('.'); dot != std::string::npos && dot > 0)
            AddRule(rules, identity.computerName.substr(0, dot), "<computer>");
    }
    if (!identity.userName.empty())
        AddRule(rules, identity.userName, "<user>");
    // The longest match first, so a full path is replaced before the name it ends with.
    std::stable_sort(
        rules.begin(), rules.end(), [](const Rule& a, const Rule& b) { return a.needle.size() > b.needle.size(); });

    std::string out;
    out.reserve(text.size());
    for (size_t at = 0; at < text.size();)
    {
        const Rule* match = nullptr;
        for (const Rule& rule : rules)
        {
            if (!MatchesAt(text, at, rule.needle))
                continue;
            bool starts = at == 0 || !WordChar(text[at - 1]);
            bool ends = at + rule.needle.size() == text.size() || !WordChar(text[at + rule.needle.size()]);
            if (!starts || !ends)
                continue;
            match = &rule;
            break;
        }
        if (match != nullptr)
        {
            out += match->replacement;
            at += match->needle.size();
        }
        else
            out += text[at++];
    }
    return out;
}

launcher_report::Identity launcher_report::CurrentIdentity(const std::string& iso)
{
    Identity identity;
    identity.userName = UserName();
    identity.homeFolder = HomeFolder();
    identity.computerName = HostName();
    identity.gamesFolder = datadir::Text(GamesFolder());
    if (!iso.empty())
    {
        std::error_code error;
        fs::path folder = fs::path(std::u8string(iso.begin(), iso.end())).parent_path();
        if (!folder.empty())
            identity.isoFolder = datadir::Text(folder);
    }
    return identity;
}

launcher_report::Versions launcher_report::ReadVersions(
    const std::string& launcherVersion, const std::string& kitVersion, const fs::path& runtimeFolder)
{
    Versions versions;
    versions.launcher = launcherVersion;
    versions.kit = kitVersion;
    versions.os = SDL_GetPlatform();
    std::string log = ReadFile(runtime_names::ReadLog(runtimeFolder), 256 * 1024);
    versions.gameBuild = FindLogLine(log, "[version] ");
    if (versions.gameBuild.empty())
        versions.gameBuild = "(no game log yet)";
    versions.cpu = FindLogLine(log, "[system] CPU ");
    versions.gpu = FindLogLine(log, "[gpu] adapter ");
    if (versions.gpu.empty())
        versions.gpu = FindLogLine(log, "[gpu] ");
    if (versions.gpu.empty())
        versions.gpu = "(no GPU line in the log)";
    return versions;
}

void launcher_report::AddSettingsFolders(Identity& identity, const std::filesystem::path& settingsFile)
{
    // launcher.ini goes in whole: the folders it names (every game's disc, the Games folder) may be the player's name.
    std::istringstream settings(ReadFile(settingsFile));
    for (std::string line; std::getline(settings, line);)
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        std::string key = line.substr(0, equals), folder = line.substr(equals + 1);
        key.erase(key.find_last_not_of(" \t") + 1);
        folder.erase(0, folder.find_first_not_of(" \t"));
        const size_t slash = folder.find_last_of("/\\"); // both spellings: a report can quote another system's path
        if (slash == std::string::npos || (key != "iso" && key != "games" && key != "kit"))
            continue;
        if (key == "kit" && !identity.homeFolder.empty() && folder.starts_with(identity.homeFolder))
            continue; // already "~", and where in it helps
        if (key == "iso")
            folder.erase(slash);
        if (folder.size() > 3) // never a bare drive: "D:" would be replaced all over the report
            identity.otherFolders.push_back(folder);
    }
}

std::string launcher_report::Anonymous(const std::string& text, Identity identity)
{
    // A log can quote a disc that launcher.ini no longer names: every disc image's folder goes.
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);)
    {
        std::string lower = line;
        for (char& c : lower)
            c = c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
        size_t from = 0;
        for (size_t end; (end = lower.find(".iso", from)) != std::string::npos; from = end + 4)
        {
            size_t start = std::string::npos;
            for (size_t at = from; at < end && start == std::string::npos; at++)
            {
                const bool opens = at == 0 || std::string(" \t=\"'([").find(line[at - 1]) != std::string::npos;
                const bool drive = at + 2 < end && line[at + 1] == ':' && (line[at + 2] == '/' || line[at + 2] == '\\');
                if (opens && (drive || line[at] == '/' || line.compare(at, 2, "\\\\") == 0))
                    start = at;
            }
            if (start == std::string::npos)
                continue;
            const size_t slash = line.find_last_of("/\\", end);
            if (slash != std::string::npos && slash > start + 3)
                identity.otherFolders.push_back(line.substr(start, slash - start));
        }
    }
    return HideDrives(Redact(text, identity));
}

bool launcher_report::AnonymiseFile(const std::filesystem::path& file, const Identity& identity)
{
    std::error_code error;
    const auto written = fs::last_write_time(file, error);
    if (error)
        return false;
    std::string text;
    {
        std::ifstream in(file, std::ios::binary);
        if (!in)
            return false;
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::string clean = Anonymous(text, identity);
    if (clean == text)
        return false;
    fs::path next = file;
    next += ".new";
    {
        std::ofstream out(next, std::ios::binary | std::ios::trunc);
        out.write(clean.data(), std::streamsize(clean.size()));
        out.close();
        if (out.fail())
        {
            fs::remove(next, error);
            return false;
        }
    }
    fs::rename(next, file, error);
    if (error)
    {
        fs::remove(next, error);
        return false;
    }
    fs::last_write_time(file, written, error);
    return true;
}

std::string launcher_report::BuildReport(const Sources& sources)
{
    std::string report;
    report += "Arkeyan Launcher problem report\n";
    report += "Generated: " + Stamp() + "\n";
    report += "Launcher: " + (sources.versions.launcher.empty() ? "(unknown)" : sources.versions.launcher) + "\n";
    report += "Kit: " + (sources.versions.kit.empty() ? "(no kit)" : sources.versions.kit) + "\n";
    report += "Game build: " + (sources.versions.gameBuild.empty() ? "(unknown)" : sources.versions.gameBuild) + "\n";
    report += "OS: " + (sources.versions.os.empty() ? "(unknown)" : sources.versions.os) + "\n";
    report += "CPU: " + (sources.versions.cpu.empty() ? "(unknown)" : sources.versions.cpu) + "\n";
    report += "GPU: " + (sources.versions.gpu.empty() ? "(unknown)" : sources.versions.gpu) + "\n";
    if (!sources.iso.empty())
        report += "Disc image: " + sources.iso + "\n";

    // The last crash: the fault block of this run's log, else of the run before (the next start moves a crashed log aside).
    std::string crash = launcher_report::CrashDetails(ReadFile(runtime_names::ReadLog(sources.runtimeFolder)));
    std::string crashFrom = "this run";
    if (crash.empty())
    {
        crash = launcher_report::CrashDetails(ReadFile(runtime_names::ReadLog(sources.runtimeFolder, true)));
        crashFrom = "the previous run";
    }
    report += crash.empty() ? "Last crash: none in the logs\n\n" : "Last crash: yes (" + crashFrom + "), see below\n\n";
    report += crash.empty() ? "===== last crash =====\n(no crash in the logs)\n\n"
                            : "===== last crash (" + crashFrom + ") =====\n" + crash + "\n\n";

    Section(report, "launcher.ini", sources.settingsFile);
    Section(report, "migration.log", sources.logFolder / "migration.log");
    // One copy of the last run (the game's own log and the launcher's capture hold the same lines), then the run before.
    const GameLog lastRun = !sources.lastRun.file.empty() || !sources.lastRun.game.empty()
                                ? sources.lastRun
                                : NewestGameLog(sources.logFolder / CaptureLogName,
                                      { { std::string(), runtime_names::ReadLog(sources.runtimeFolder) } });
    if (lastRun.file.empty())
        report += "===== Game log =====\n(empty or not found)\n\n";
    else
        Section(report, "Game log: " + GameLogHeading(lastRun), lastRun.file);
    Section(report, runtime_names::ReadLog(sources.runtimeFolder, true).filename().string(),
        runtime_names::ReadLog(sources.runtimeFolder, true));
    Section(report, "install.log", sources.runtimeFolder / "install.log");
    Section(report, "install.prev.log", sources.runtimeFolder / "install.prev.log");

    // The step logs Arkeyan-Builder kept after a successful install.
    std::vector<fs::path> kept;
    std::error_code error;
    for (fs::directory_iterator it(sources.runtimeFolder / "install-logs", error), end; !error && it != end;
        it.increment(error))
        if (it->is_regular_file(error))
            kept.push_back(it->path());
    std::sort(kept.begin(), kept.end());
    for (const fs::path& file : kept)
        Section(report, "install-logs/" + datadir::Text(file.filename()), file);

    Identity identity;
    identity.userName = sources.userName;
    identity.computerName = sources.computerName;
    identity.homeFolder = sources.homeFolder;
    AddSettingsFolders(identity, sources.settingsFile);
    if (!sources.iso.empty())
    {
        fs::path folder = fs::path(std::u8string(sources.iso.begin(), sources.iso.end())).parent_path();
        if (!folder.empty())
            identity.isoFolder = datadir::Text(folder);
    }
    return Anonymous(Sanitise(report), identity) + "\n";
}

std::string launcher_report::CrashDetails(const std::string& log)
{
    size_t at = std::string::npos;
    for (size_t found = log.find("Fault ("); found != std::string::npos; found = log.find("Fault (", found + 1))
        at = found;
    return at == std::string::npos ? std::string() : log.substr(at);
}

fs::path launcher_report::SaveToFirstFolder(
    const std::string& text, const std::string& name, const std::vector<fs::path>& folders)
{
    for (const fs::path& folder : folders)
    {
        if (folder.empty())
            continue;
        std::error_code error;
        fs::create_directories(folder, error);
        fs::path file = folder / name;
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(text.data(), std::streamsize(text.size()));
        out.close();
        if (out)
            return file;
    }
    return {};
}

std::string launcher_report::FileName() { return "ArkeyanLauncher-report-" + FileStamp() + ".txt"; }
