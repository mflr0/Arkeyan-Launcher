#include "launcher/portable_export.h"
#include "launcher/game_keys.h"
#include "launcher/game_names.h"
#include "launcher/i18n.h"
#include "launcher/ui/launcher_ui.h"
#include "shared/collection/datadir.h"
#include "shared/vfs.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <ctime>

namespace fs = std::filesystem;

const char* PortableExtension()
{
#if defined(_WIN32)
    return ".exe";
#elif defined(__APPLE__)
    return ".app";
#else
    return ".AppImage";
#endif
}

std::string PortableName(std::string_view gameId)
{
    const char* full = GameName(gameId);
    std::string name;
    for (const char* c = full != nullptr ? full : "Skylanders"; *c != 0; c++)
        if (std::string_view("<>:\"/\\|?*").find(*c) == std::string_view::npos)
            name += *c;
    return name;
}

std::string PortableKeys(const LauncherConfig& config, std::string_view gameId)
{
    LauncherConfig keys = config;
    SeedGameInput(keys, gameId);
    const std::string section = GameInputSection(gameId);
    std::string text = "[" + section + "]\n";
    for (const auto& [key, value] : keys[section])
        text += key + "=" + value + "\n";
    return text;
}

std::string PortableStamp()
{
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#if defined(_WIN32)
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &parts);
    return text;
}

namespace
{
    constexpr uint64_t Reserve = 64ull << 20; // kept free besides the portable

    // <app>/Contents/MacOS/<name>: the bundle; empty for an executable that is not in one.
    fs::path BundleOf(const fs::path& runtime)
    {
        const fs::path macos = runtime.parent_path(), contents = macos.parent_path(), app = contents.parent_path();
        return macos.filename() == "MacOS" && contents.filename() == "Contents" && app.extension() == ".app" ? app : fs::path();
    }

    uint64_t TreeSize(const fs::path& path)
    {
        std::error_code error;
        if (fs::is_regular_file(path, error))
            return fs::file_size(path, error);
        uint64_t total = 0;
        for (fs::recursive_directory_iterator it(path, error), end; !error && it != end; it.increment(error))
            if (it->is_regular_file(error))
                total += it->file_size(error);
        return total;
    }

    // The collection's own files: the figures and collection.json; not the locks, the unfinished writes or the backups' folder.
    std::vector<fs::path> CollectionFiles(const fs::path& folder)
    {
        std::vector<fs::path> files;
        std::error_code error;
        for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error))
        {
            const std::string name = datadir::Text(it->path().filename());
            if (it->is_regular_file(error) && !name.starts_with(".") && !name.ends_with(".lock") && !name.ends_with(".part") &&
                !name.ends_with(".tmp"))
                files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    // The bundle signed again (ad hoc, as Arkeyan-Builder signs it): its seal now covers the pack.
    bool Sign(const fs::path& app)
    {
        const std::string path = datadir::Text(app);
        const char* arguments[] = { "/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", path.c_str(), nullptr };
        SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arguments);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_Process* process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        int code = -1;
        if (process != nullptr)
        {
            SDL_WaitProcess(process, true, &code);
            SDL_DestroyProcess(process);
        }
        return code == 0;
    }
}

PortableResult ExportPortable(const PortableSpec& spec, const std::atomic<bool>* cancel, const std::function<void(uint64_t, uint64_t)>& progress)
{
    using Outcome = PortableResult::Outcome;
    PortableResult result;
    auto fail = [&](std::string text) {
        result.outcome = Outcome::Failed;
        result.error = std::move(text);
        return result;
    };

    DiscFileSystem disc;
    if (!disc.Open(spec.source.c_str()))
        return fail(Tr("couldn't open the disc image"));
    const auto files = disc.Files();
    std::error_code error;
    const fs::path bundle = BundleOf(spec.runtime);
    if (!fs::is_regular_file(spec.runtime, error))
        return fail(Tr("the installed game is missing: install it again"));
#if defined(__APPLE__)
    if (bundle.empty())
        return fail(Tr("the installed game is missing: install it again"));
#endif
    // Never into the installed game's folder: the copy, named like the game, would replace the game it is made from.
    const fs::path installed = bundle.empty() ? spec.runtime.parent_path() : bundle.parent_path();
    {
        const fs::path relative = fs::weakly_canonical(spec.folder, error).lexically_relative(fs::weakly_canonical(installed, error));
        if (error || (!relative.empty() && *relative.begin() != ".."))
            return fail(Tr("choose a folder outside the installed game"));
    }
    const std::vector<fs::path> figures = CollectionFiles(spec.collection);
    const fs::path icon = installed / "game_icon.png";
    const fs::path notices = spec.runtime.parent_path() / "THIRD_PARTY_NOTICES.md"; // the .app holds its own

    // The room first: nothing is written when it is short.
    uint64_t total = TreeSize(bundle.empty() ? spec.runtime : bundle) + TreeSize(icon) + TreeSize(notices);
    for (const auto& [path, entry] : files)
        total += entry->size;
    for (const fs::path& figure : figures)
        total += TreeSize(figure);
    fs::create_directories(spec.folder, error);
    const int64_t room = spec.freeBytes >= 0 ? spec.freeBytes : int64_t(fs::space(spec.folder, error).available);
    if (uint64_t(std::max<int64_t>(room, 0)) < total + Reserve)
    {
        result.outcome = Outcome::NotEnoughSpace;
        result.needed = total + Reserve;
        result.available = uint64_t(std::max<int64_t>(room, 0));
        return result;
    }

    const fs::path target = spec.folder / datadir::FromText(spec.name + PortableExtension());
    const fs::path work = spec.folder / datadir::FromText("." + spec.name + ".export");
    auto cleanup = [&] {
        std::error_code ignored;
        fs::remove_all(work, ignored);
    };
    cleanup();
    fs::create_directories(work, error);
    const fs::path made = work / target.filename();
    auto report = [&](uint64_t done) {
        if (progress)
            progress(done, total);
    };

    // The game: its bundle (macOS), else its executable, which the pack follows.
    fs::path packFile = made, program = made;
    if (!bundle.empty())
    {
        fs::copy(bundle, made, fs::copy_options::recursive | fs::copy_options::copy_symlinks, error);
        packFile = made / "Contents" / "Resources" / portable::PackName;
        program = made / "Contents" / "MacOS" / spec.runtime.filename();
    }
    else
        fs::copy_file(spec.runtime, made, error);
    if (error)
    {
        cleanup();
        return fail(Tf("couldn't copy the game: {}", error.message()));
    }
    const uint64_t copied = TreeSize(made);
    report(copied);

    portable::Writer writer;
    writer.cancel = cancel;
    writer.progress = [&](uint64_t written) { report(copied + written); };
    std::string why;
    size_t count = 0;
    bool ok = writer.Open(packFile, bundle.empty(), why);
    for (const auto& [path, entry] : files)
    {
        const DiscFileSystem::Entry* file = entry;
        ok = ok && writer.Add(portable::GamePrefix + path, file->size, [&](uint64_t position, void* buffer, size_t size) {
            return size_t(disc.Read(*file, position, buffer, uint32_t(size)));
        }, why);
        count++;
    }
    ok = ok && writer.AddText(portable::InfoName, portable::FormatInfo(spec.info), why) && writer.AddText(portable::KeysName, spec.keys, why);
    count += 2;
    if (fs::is_regular_file(icon, error))
    {
        ok = ok && writer.AddFile(portable::IconName, icon, why);
        count++;
    }
    if (bundle.empty() && fs::is_regular_file(notices, error))
    {
        ok = ok && writer.AddFile("THIRD_PARTY_NOTICES.md", notices, why);
        count++;
    }
    for (const fs::path& figure : figures)
    {
        ok = ok && writer.AddFile(portable::CollectionPrefix + datadir::Text(figure.filename()), figure, why);
        count++;
    }
    ok = ok && writer.Finish(why);
    if (!ok)
    {
        writer.Close();
        cleanup();
        if (cancel != nullptr && cancel->load())
        {
            result.outcome = Outcome::Cancelled;
            return result;
        }
        return fail(why);
    }

    // Read back: the index the game will read, every file there.
    std::vector<portable::Entry> entries;
    if (!portable::ReadIndex(packFile, entries, &why) || entries.size() != count)
    {
        cleanup();
        return fail(Tf("the copy can't be read back: {}", why));
    }
    fs::permissions(program, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, error);
#if defined(__APPLE__)
    if (spec.sign && !Sign(made))
    {
        cleanup();
        return fail(Tr("couldn't sign the app (codesign)"));
    }
#endif
    // The old copy goes aside first and comes back if the new one can't take its place (a scanner holding the new file).
    const fs::path previous = work / "previous";
    const bool replacing = fs::exists(fs::symlink_status(target, error));
    error.clear();
    if (replacing)
        fs::rename(target, previous, error);
    for (int attempt = 0; !error && attempt < 5; ++attempt)
    {
        fs::rename(made, target, error);
        if (!error)
            break;
        if (attempt < 4)
        {
            error.clear();
            SDL_Delay(200);
        }
    }
    if (error && replacing)
    {
        std::error_code ignored;
        fs::rename(previous, target, ignored);
    }
    cleanup();
    if (error)
        return fail(Tf("couldn't put the copy in place: {}", error.message()));
    result.outcome = Outcome::Done;
    result.path = target;
    result.bytes = TreeSize(target);
    report(total);
    return result;
}

float PortableExportJob::Fraction() const
{
    const uint64_t total = m_total;
    return total == 0 ? 0.0f : float(double(m_bytes.load()) / double(total));
}

void PortableExportJob::Start(const PortableSpec& spec)
{
    Join();
    m_started = true;
    m_done = false;
    m_cancel = false;
    m_bytes = m_total = 0;
    m_result = {};
    m_thread = std::thread([this, spec] {
        m_result = ExportPortable(spec, &m_cancel, [this](uint64_t done, uint64_t total) {
            m_total = total;
            m_bytes = done;
        });
        m_done = true;
    });
}

PortableResult PortableExportJob::Collect()
{
    Join();
    PortableResult result = m_result;
    m_started = m_done = false;
    return result;
}

std::string PortableFailureText(const PortableResult& result)
{
    using Outcome = PortableResult::Outcome;
    if (result.outcome == Outcome::NotEnoughSpace)
        return Tf("Not enough space: {} needed, {} free.", LauncherFormatSize(result.needed), LauncherFormatSize(result.available));
    if (result.outcome == Outcome::Failed)
        return Tf("Couldn't export the game: {}", result.error);
    return {};
}
