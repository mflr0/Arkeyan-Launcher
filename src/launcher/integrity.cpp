#include "launcher/integrity.h"
#include "shared/dir_list.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

namespace fs = std::filesystem;

namespace
{
    std::string Generic(const fs::path& path)
    {
        std::u8string text = path.generic_u8string();
        return std::string(text.begin(), text.end());
    }

    // Every file under `folder`, as paths relative to `base`.
    void AddTree(const fs::path& base, const fs::path& folder, std::vector<std::string>& files)
    {
        std::error_code error;
        for (const auto& entry : ListDirectory(folder, error))
        {
            std::error_code kind;
            if (entry.is_symlink(kind))
                continue;
            if (entry.is_directory(kind))
                AddTree(base, entry.path(), files);
            else if (entry.is_regular_file(kind))
                files.push_back(Generic(entry.path().lexically_relative(base)));
        }
    }

    // `path` relative to `base` when it is inside it, else empty.
    fs::path Inside(const fs::path& base, const fs::path& path)
    {
        fs::path relative = path.lexically_normal().lexically_relative(base.lexically_normal());
        if (relative.empty() || *relative.begin() == "..")
            return {};
        return relative;
    }
}

std::vector<std::string> InstalledGameFiles(const fs::path& gameFolder, const fs::path& runtime)
{
    std::vector<std::string> files;
    const fs::path relative = Inside(gameFolder, runtime);
    if (relative.empty())
        return files;
    // <name>.app/Contents/MacOS/<name>: the whole bundle is the game.
    fs::path bundle;
    for (const fs::path& part : relative)
    {
        bundle /= part;
        if (part.extension() == ".app")
            break;
    }
    std::error_code error;
    if (bundle.extension() == ".app" && fs::is_directory(gameFolder / bundle, error))
        AddTree(gameFolder, gameFolder / bundle, files);
    else
        files.push_back(Generic(relative));
    for (const char* name : { "THIRD_PARTY_NOTICES.md", "game_icon.png", "game_icon.ico", "game_icon.icns", "game.desktop" })
        if (fs::is_regular_file(gameFolder / name, error))
            files.push_back(name);
    AddTree(gameFolder, gameFolder / "fonts", files);
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

bool WriteInstalledGameList(const fs::path& gameFolder, const fs::path& runtime, std::string& error)
{
    std::vector<std::string> files = InstalledGameFiles(gameFolder, runtime);
    if (files.empty())
    {
        error = "the game is not in its folder";
        return false;
    }
    return filelist::Write(gameFolder, files, error);
}

IntegrityFinding CheckFolder(const IntegrityTarget& target, const std::atomic<bool>* cancel)
{
    filelist::Result result = filelist::Check(target.folder, cancel);
    return { target.id, target.folder, result.cancelled ? std::vector<filelist::Problem>() : std::move(result.problems) };
}

IntegrityCheck::~IntegrityCheck()
{
    m_cancel = true;
    if (m_thread.joinable())
        m_thread.join();
    if (m_writer.joinable())
        m_writer.join();
}

void IntegrityCheck::WriteGameList(const fs::path& gameFolder, const fs::path& runtime)
{
    if (m_writer.joinable())
        m_writer.join();
    m_writing = true;
    m_writer = std::thread([this, gameFolder, runtime] {
        if (std::string error; !WriteInstalledGameList(gameFolder, runtime, error))
            fprintf(stderr, "launcher: no file list for %s: %s\n", gameFolder.filename().string().c_str(), error.c_str());
        m_writing = false;
    });
}

void IntegrityCheck::Start(std::vector<IntegrityTarget> targets)
{
    if (m_running)
        return;
    if (m_thread.joinable())
        m_thread.join();
    if (m_writer.joinable())
        m_writer.join();
    m_running = true;
    m_cancel = false;
    {
        std::lock_guard lock(m_mutex);
        m_report = {};
    }
    m_thread = std::thread([this, targets = std::move(targets)] {
        const auto start = std::chrono::steady_clock::now();
        Report report;
        for (const IntegrityTarget& target : targets)
        {
            filelist::Result result = filelist::Check(target.folder, &m_cancel);
            if (result.cancelled)
                break;
            report.files += result.files;
            report.hashed += result.hashed;
            report.hashedBytes += result.hashedBytes;
            if (!result.problems.empty())
                report.findings.push_back({ target.id, target.folder, std::move(result.problems) });
        }
        report.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        report.done = true;
        std::lock_guard lock(m_mutex);
        m_report = std::move(report);
        m_running = false;
    });
}

IntegrityCheck::Report IntegrityCheck::Snapshot()
{
    std::lock_guard lock(m_mutex);
    return m_report;
}
