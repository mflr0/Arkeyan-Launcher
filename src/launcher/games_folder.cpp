#include "launcher/games_folder.h"
#include "launcher/game_install.h"
#include "launcher/i18n.h"
#include "shared/collection/datadir.h"
#include "shared/dir_list.h"
#include <algorithm>
#include <fstream>
#include <memory>
#if defined(_WIN32)
#include <cwctype>
#else
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace gamesfolder
{
    namespace
    {
        fs::path Resolved(const fs::path& path)
        {
            std::error_code error;
            fs::path resolved = fs::weakly_canonical(path, error);
            return error ? path.lexically_normal() : resolved;
        }

        // `path` is `folder` or inside it, links resolved on both.
        bool IsOrInside(const fs::path& path, const fs::path& folder)
        {
            if (path.empty() || folder.empty())
                return false;
            const fs::path relative = Resolved(path).lexically_relative(Resolved(folder));
            return !relative.empty() && *relative.begin() != "..";
        }

        // The data folder's <Game> or the games folder's (`root` empty: the data folder).
        fs::path GameFolderIn(const fs::path& root, std::string_view id)
        {
            return root.empty() ? GameDataFolder(id) : root / datadir::GameFolderName(id);
        }

        uint64_t BytesOf(const fs::path& path)
        {
            std::error_code error;
            const fs::file_status status = fs::symlink_status(path, error);
            if (fs::is_regular_file(status))
                return fs::file_size(path, error);
            if (!fs::is_directory(status))
                return 0;
            uint64_t total = 0;
            for (const fs::directory_entry& entry : ListDirectory(path, error))
                total += BytesOf(entry.path());
            return total;
        }

        // The nearest folder of `path` that exists (the place a new folder would be made in).
        fs::path Existing(fs::path path)
        {
            std::error_code error;
            while (!path.empty() && !fs::exists(path, error) && path.has_parent_path() && path.parent_path() != path)
                path = path.parent_path();
            return path;
        }

        bool SameVolume(const fs::path& a, const fs::path& b)
        {
#if defined(_WIN32)
            auto drive = [](const fs::path& path) {
                std::wstring name = Resolved(path).root_name().native();
                for (wchar_t& c : name)
                    c = wchar_t(std::towlower(c));
                return name;
            };
            return drive(a) == drive(b);
#else
            struct stat first{}, second{};
            return stat(Existing(a).c_str(), &first) == 0 && stat(Existing(b).c_str(), &second) == 0 &&
                   first.st_dev == second.st_dev;
#endif
        }

        // What belongs to the built game in a game's data folder (InstalledGameFiles, integrity.cpp, has the same list),
        // the build's work folder, and the two that follow the games folder.
        constexpr const char* BuiltNames[] = { "THIRD_PARTY_NOTICES.md", "game_icon.png", "game_icon.ico",
            "game_icon.icns", "game.desktop", "fonts", "files.sha256", "arkeyan-work", "ssabuild-work" };

        // A build of the game, in `folder`, came from a kit before the games folder: its textures-hd and gamedata stay put.
        bool OlderBuildIn(const LauncherConfig& config, const fs::path& folder)
        {
            for (const auto& [name, values] : config)
            {
                auto runtime = values.find("runtime");
                if (name.rfind("install.", 0) != 0 || runtime == values.end() || runtime->second.empty())
                    continue;
                const fs::path relative =
                    datadir::FromText(runtime->second).lexically_normal().lexically_relative(folder.lexically_normal());
                if (relative.empty() || *relative.begin() == "..")
                    continue;
                if (auto known = values.find("games_folder"); known == values.end() || known->second != "1")
                    return true;
            }
            return false;
        }

        // The top-level names of the built game in `folder`: the recorded runtime's, the runtime names of the game.
        std::vector<std::string> RuntimeNames(
            const LauncherConfig& config, const GameInfo& game, const fs::path& folder)
        {
            std::vector<std::string> names;
            for (const auto& [name, values] : config)
            {
                auto runtime = values.find("runtime");
                if (name.rfind("install.", 0) != 0 || runtime == values.end() || runtime->second.empty())
                    continue;
                const fs::path relative =
                    datadir::FromText(runtime->second).lexically_normal().lexically_relative(folder.lexically_normal());
                if (!relative.empty() && *relative.begin() != ".." && *relative.begin() != ".")
                    names.push_back(datadir::Text(*relative.begin()));
            }
            for (const char* runtime : { game.runtime, game.legacyRuntime })
                if (runtime != nullptr)
                    for (const char* suffix : { "", ".exe", ".app" })
                        names.push_back(std::string(runtime) + suffix);
            return names;
        }

        // ---- Copying, checked byte for byte ------------------------------------------------------------------------------

        struct Copier
        {
            const std::atomic<bool>& stop;
            const Options& options;
            uint64_t& done;
            std::string error;

            void Report()
            {
                if (options.progress)
                    options.progress(done);
            }

            bool File(const fs::path& from, const fs::path& to)
            {
                std::ifstream in(from, std::ios::binary);
                std::ofstream out(to, std::ios::binary | std::ios::trunc);
                if (!in || !out)
                {
                    error = "can't open " + datadir::Text(!in ? from : to);
                    return false;
                }
                std::vector<char> buffer(1 << 20);
                while (in)
                {
                    if (stop)
                        return false;
                    in.read(buffer.data(), std::streamsize(buffer.size()));
                    const std::streamsize read = in.gcount();
                    if (read <= 0)
                        break;
                    out.write(buffer.data(), read);
                    if (!out)
                    {
                        error = "can't write " + datadir::Text(to);
                        return false;
                    }
                    done += uint64_t(read);
                    Report();
                }
                out.close();
                if (!out || in.bad())
                {
                    error = "can't write " + datadir::Text(to);
                    return false;
                }
                std::error_code ignored;
                fs::permissions(to, fs::status(from, ignored).permissions(), ignored); // a game stays runnable
                return Same(from, to);
            }

            bool Same(const fs::path& a, const fs::path& b)
            {
                std::ifstream first(a, std::ios::binary), second(b, std::ios::binary);
                std::vector<char> one(1 << 20), two(1 << 20);
                while (first && second)
                {
                    if (stop)
                        return false;
                    first.read(one.data(), std::streamsize(one.size()));
                    second.read(two.data(), std::streamsize(two.size()));
                    if (first.gcount() != second.gcount() ||
                        !std::equal(one.begin(), one.begin() + first.gcount(), two.begin()))
                    {
                        error = "the copy of " + datadir::Text(a) + " differs";
                        return false;
                    }
                }
                if (first.eof() != second.eof())
                {
                    error = "the copy of " + datadir::Text(a) + " differs";
                    return false;
                }
                return true;
            }

            bool Tree(const fs::path& from, const fs::path& to)
            {
                std::error_code e;
                const fs::file_status status = fs::symlink_status(from, e);
                if (fs::is_symlink(status))
                {
                    fs::copy_symlink(from, to, e);
                    if (e)
                        error = e.message();
                    return !e;
                }
                if (fs::is_directory(status))
                {
                    fs::create_directories(to, e);
                    if (e)
                    {
                        error = e.message();
                        return false;
                    }
                    for (const fs::directory_entry& entry : ListDirectory(from, e))
                        if (!Tree(entry.path(), to / entry.path().filename()))
                            return false;
                    return !stop;
                }
                return File(from, to);
            }
        };

        // The name an entry is staged under, beside its place, until the whole move is done.
        fs::path Staged(const fs::path& to)
        {
            fs::path staged = to.parent_path();
            staged /= datadir::FromText("." + datadir::Text(to.filename()) + ".moving");
            return staged;
        }
    }

    Refusal Check(const fs::path& folder)
    {
        if (folder.empty())
            return Refusal::None;
        std::error_code error;
        if (!fs::is_directory(folder, error))
            return Refusal::Missing;
        for (const GameInfo& game : g_games)
            if (IsOrInside(folder, GameDataFolder(game.id)) || IsOrInside(folder, GameFilesFolder(game.id)))
                return Refusal::InGame;
        if (IsOrInside(folder, DownloadedKitsFolder()))
            return Refusal::InKits;
        const fs::path probe = folder / ".arkeyan-write-check";
        {
            std::ofstream file(probe, std::ios::binary | std::ios::trunc);
            if (!file || !(file << "ok") || !file.flush())
            {
                file.close();
                fs::remove(probe, error);
                return Refusal::ReadOnly;
            }
        }
        fs::remove(probe, error);
        return Refusal::None;
    }

    const char* RefusalText(Refusal refusal)
    {
        switch (refusal)
        {
        case Refusal::None: return "";
        case Refusal::Missing: return Tr("This folder isn't there");
        case Refusal::ReadOnly: return Tr("This folder can't be written to");
        case Refusal::InGame: return Tr("Choose a folder outside the games");
        case Refusal::InKits: return Tr("Choose a folder outside the game tools");
        }
        return "";
    }

    Plan MakePlan(const LauncherConfig& config, const fs::path& from, const fs::path& to)
    {
        Plan plan{ from, to, {}, 0 };
        if (from.lexically_normal() == to.lexically_normal())
            return plan;
        for (const GameInfo& game : g_games)
        {
            const fs::path source = GameFolderIn(from, game.id), target = GameFolderIn(to, game.id);
            std::error_code error;
            if (!fs::is_directory(source, error) || fs::is_symlink(fs::symlink_status(source, error)) ||
                Resolved(source) == Resolved(target))
                continue;
            std::vector<std::string> names;
            if (from.empty())
            {
                // The data folder holds the player's things too: only what is the build's leaves it.
                names = RuntimeNames(config, game, source);
                names.insert(names.end(), std::begin(BuiltNames), std::end(BuiltNames));
                if (!OlderBuildIn(config, source))
                    names.insert(names.end(), { "textures-hd", "gamedata" });
            }
            else
                for (const fs::directory_entry& entry : ListDirectory(source, error))
                    names.push_back(datadir::Text(entry.path().filename()));
            std::sort(names.begin(), names.end());
            names.erase(std::unique(names.begin(), names.end()), names.end());
            for (const std::string& name : names)
            {
                const fs::path path = source / datadir::FromText(name);
                if (!fs::exists(fs::symlink_status(path, error)) || name.ends_with(".moving"))
                    continue;
                Entry entry{ game.id, path, target / datadir::FromText(name), BytesOf(path) };
                plan.bytes += entry.bytes;
                plan.entries.push_back(std::move(entry));
            }
        }
        return plan;
    }

    Result Move(const Plan& plan, const Options& options, const std::atomic<bool>& stop)
    {
        Result result;
        // The room first: what can't be renamed is copied, and the old copy stays until the new one is checked.
        for (const Entry& entry : plan.entries)
            if (options.forceCopy || !SameVolume(entry.from, entry.to.parent_path()))
                result.needed += entry.bytes;
        if (result.needed > 0)
        {
            const fs::path target = Existing(plan.entries.front().to.parent_path());
            std::error_code error;
            const int64_t room =
                options.freeBytes >= 0 ? options.freeBytes : int64_t(fs::space(target, error).available);
            constexpr uint64_t Reserve = 64ull << 20; // kept free besides the games
            if (error || room < 0 || uint64_t(room) < result.needed + (options.freeBytes >= 0 ? 0 : Reserve))
            {
                result.state = Result::State::NoRoom;
                return result;
            }
        }

        struct Done
        {
            const Entry* entry;
            fs::path staged;
            bool renamed;
        };
        std::vector<Done> staged;
        uint64_t moved = 0;
        // Everything back as it was: what was renamed returns, the copies made so far go.
        auto undo = [&] {
            std::error_code error;
            for (auto it = staged.rbegin(); it != staged.rend(); ++it)
                if (it->renamed)
                    fs::rename(it->staged, it->entry->from, error);
                else
                    fs::remove_all(it->staged, error);
            for (const Entry& entry : plan.entries) // the game folders made for the move, still empty
                if (fs::is_directory(entry.to.parent_path(), error) && fs::is_empty(entry.to.parent_path(), error))
                    fs::remove(entry.to.parent_path(), error);
        };
        for (const Entry& entry : plan.entries)
        {
            std::error_code error;
            if (stop)
            {
                undo();
                result.state = Result::State::Stopped;
                return result;
            }
            fs::create_directories(entry.to.parent_path(), error);
            const fs::path temporary = Staged(entry.to);
            fs::remove_all(temporary, error); // left by a launcher that was closed half way
            if (!options.forceCopy)
            {
                fs::rename(entry.from, temporary, error);
                if (!error)
                {
                    staged.push_back({ &entry, temporary, true });
                    moved += entry.bytes;
                    if (options.progress)
                        options.progress(moved);
                    continue;
                }
            }
            Copier copier{ stop, options, moved, {} };
            const bool copied = copier.Tree(entry.from, temporary);
            staged.push_back({ &entry, temporary, false });
            if (!copied || stop)
            {
                undo();
                result.state = stop ? Result::State::Stopped : Result::State::Failed;
                result.error = copier.error;
                return result;
            }
        }
        if (stop)
        {
            undo();
            result.state = Result::State::Stopped;
            return result;
        }

        // Every entry is at its new place under its staging name: each takes its own name, then its old copy goes,
        // only once the new one is in place (a failed rename keeps both).
        for (const Done& done : staged)
        {
            std::error_code error;
            if (fs::exists(fs::symlink_status(done.entry->to, error)))
                fs::remove_all(done.entry->to, error); // an older copy left there: the one coming is the game's
            fs::rename(done.staged, done.entry->to, error);
            if (error)
            {
                if (result.error.empty())
                {
                    result.state = Result::State::Failed;
                    result.error = error.message();
                }
                continue;
            }
            if (!done.renamed)
                fs::remove_all(done.entry->from, error);
        }
        // A game's folder in the old games folder goes once it is empty; the games folder itself stays.
        if (!plan.from.empty())
            for (const Entry& entry : plan.entries)
            {
                std::error_code error;
                if (fs::is_directory(entry.from.parent_path(), error) && fs::is_empty(entry.from.parent_path(), error))
                    fs::remove(entry.from.parent_path(), error);
            }
        return result;
    }

    void Record(LauncherConfig& config, const Plan& plan)
    {
        for (auto& [section, values] : config)
            for (auto& [key, value] : values)
            {
                if (value.empty() || (section == "launcher" && key == "games"))
                    continue;
                const fs::path path = datadir::FromText(value).lexically_normal();
                for (const Entry& entry : plan.entries)
                {
                    const fs::path relative = path.lexically_relative(entry.from.lexically_normal());
                    if (relative.empty() || *relative.begin() == "..")
                        continue;
                    value = datadir::Text(relative == "." ? entry.to : entry.to / relative);
                    break;
                }
            }
        if (plan.to.empty())
            config["launcher"].erase("games");
        else
            config["launcher"]["games"] = datadir::Text(plan.to);
    }

    Mover::~Mover()
    {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
    }

    bool Mover::Start(Plan plan, Options options)
    {
        if (m_running)
            return false;
        if (m_thread.joinable())
            m_thread.join();
        m_stop = false;
        m_ended = false;
        m_done = 0;
        m_total = plan.bytes;
        m_running = true;
        m_thread = std::thread([this, plan = std::move(plan), options = std::move(options)]() mutable {
            auto report = options.progress;
            options.progress = [this, report](uint64_t done) {
                m_done = done;
                if (report)
                    report(done);
            };
            Result result = Move(plan, options, m_stop);
            {
                std::lock_guard lock(m_mutex);
                m_plan = std::move(plan);
                m_result = std::move(result);
            }
            m_ended = true;
            m_running = false;
        });
        return true;
    }

    bool Mover::TakeResult(Plan& plan, Result& result)
    {
        if (m_running || !m_ended)
            return false;
        std::lock_guard lock(m_mutex);
        plan = m_plan;
        result = m_result;
        m_ended = false;
        return true;
    }
}
