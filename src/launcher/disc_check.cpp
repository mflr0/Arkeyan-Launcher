#include "launcher/disc_check.h"
#include "launcher/i18n.h"
#include "launcher/sha256.h"
#include "shared/collection/game_data.h"
#include "shared/vfs.h"
#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <thread>
#include <fmt/format.h>
#include <vector>

namespace disccheck
{
    Digest ComputeDigest(const std::string& path, std::atomic<uint64_t>* done, std::atomic<uint64_t>* total,
        const std::atomic<bool>* cancel)
    {
        constexpr uint32_t BlockSize = 1u << 20;
        Digest digest;
        DiscFileSystem disc;
        if (!disc.Open(path.c_str()))
            return digest;
        const auto files = disc.Files();
        uint64_t size = 0;
        for (const auto& file : files)
            size += file.second->size;
        if (total != nullptr)
            *total = size;

        // Each file is hashed whole by one worker (the largest first, so none is left alone at the end), the workers
        // on every core but one; each opens the disc for itself. The fingerprint then takes them in path order.
        std::vector<size_t> order(files.size());
        for (size_t i = 0; i < order.size(); i++)
            order[i] = i;
        std::stable_sort(order.begin(), order.end(),
            [&](size_t a, size_t b) { return files[a].second->size > files[b].second->size; });
        std::vector<std::string> sums(files.size());
        std::vector<uint8_t> cut(files.size(), 0);
        std::atomic<size_t> next{ 0 };
        std::atomic<uint64_t> bytes{ 0 };
        std::atomic<bool> stop{ false }, unreadable{ false }, cancelled{ false };
        auto work = [&] {
            DiscFileSystem own;
            if (!own.Open(path.c_str()))
            {
                unreadable = stop = true;
                return;
            }
            const auto list = own.Files();
            std::vector<uint8_t> block(BlockSize);
            for (size_t k = next++; k < order.size() && !stop; k = next++)
            {
                const size_t i = order[k];
                const auto& entry = *list[i].second;
                Sha256 contents;
                for (uint64_t position = 0; position < entry.size;)
                {
                    if (cancel != nullptr && *cancel)
                    {
                        cancelled = stop = true;
                        return;
                    }
                    const uint32_t want = uint32_t(std::min<uint64_t>(BlockSize, entry.size - position));
                    const uint32_t got = own.Read(entry, position, block.data(), want);
                    if (got != want)
                    {
                        cut[i] = 1;
                        stop = true;
                        return;
                    }
                    contents.Update({ block.data(), got });
                    position += got;
                    const uint64_t sum = bytes += got;
                    if (done != nullptr)
                        *done = sum;
                }
                sums[i] = contents.FinishHex();
            }
        };
        const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
        const size_t count = std::min<size_t>({ std::max(1u, cores - 1), 8, std::max<size_t>(1, files.size()) });
        std::vector<std::thread> workers;
        for (size_t i = 1; i < count; i++)
            workers.emplace_back(work);
        work();
        for (std::thread& worker : workers)
            worker.join();

        digest.bytes = bytes;
        if (cancelled || (cancel != nullptr && *cancel))
        {
            digest.outcome = Digest::Outcome::Cancelled;
            return digest;
        }
        if (unreadable)
            return digest;
        for (size_t i = 0; i < files.size(); i++)
            if (cut[i] != 0)
            {
                digest.outcome = Digest::Outcome::CutShort;
                digest.file = files[i].first;
                return digest;
            }
        Sha256 whole;
        for (size_t i = 0; i < files.size(); i++)
        {
            whole.Update(files[i].first);
            whole.Update(std::string_view("\0", 1));
            whole.Update(fmt::format("{}", files[i].second->size));
            whole.Update(std::string_view("\0", 1));
            whole.Update(sums[i]);
            whole.Update("\n");
        }
        digest.files = files.size();
        digest.outcome = Digest::Outcome::Done;
        digest.sha256 = whole.FinishHex();
        return digest;
    }

    const GameRelease* ReleaseOf(const GameInfo& game, std::string_view xexSha256)
    {
        for (const GameRelease& release : game.releases)
            if (xexSha256 == release.defaultXexSha256)
                return &release;
        return nullptr;
    }

    bool HasReference(const GameInfo& game, std::string_view xexSha256)
    {
        const GameRelease* release = ReleaseOf(game, xexSha256);
        return release != nullptr && release->contentSha256 != nullptr;
    }

    Result Compare(const GameInfo& game, std::string_view xexSha256, const Digest& digest)
    {
        Result result;
        result.digest = digest.sha256;
        switch (digest.outcome)
        {
        case Digest::Outcome::Cancelled: result.verdict = Verdict::Cancelled; return result;
        case Digest::Outcome::Unreadable: result.verdict = Verdict::Unreadable; return result;
        case Digest::Outcome::CutShort: result.verdict = Verdict::CutShort; return result;
        case Digest::Outcome::Done: break;
        }
        const GameRelease* release = ReleaseOf(game, xexSha256);
        if (release == nullptr || release->contentSha256 == nullptr)
        {
            result.verdict = Verdict::NoReference;
            return result;
        }
        result.edition = release->label;
        result.verdict = digest.sha256 == release->contentSha256 ? Verdict::Matches : Verdict::Differs;
        return result;
    }

    bool Damaged(Verdict verdict)
    {
        return verdict == Verdict::Differs || verdict == Verdict::CutShort || verdict == Verdict::Unreadable;
    }

    std::string ResultText(const Result& result)
    {
        switch (result.verdict)
        {
        case Verdict::Matches: return Tf("Every file matches a good copy of this disc ({}).", result.edition);
        case Verdict::Differs: return Tf("Some files differ from a good copy of this disc ({}).", result.edition);
        case Verdict::NoReference: return Tr("Every file can be read. No good copy of this edition to compare with.");
        case Verdict::CutShort: return Tr("Part of the image can't be read.");
        case Verdict::Unreadable: return Tr("Can't read the disc image.");
        case Verdict::Cancelled: break;
        }
        return {};
    }

    std::string Stamp(const std::string& path)
    {
        namespace fs = std::filesystem;
        std::error_code error;
        fs::path file = fs::path(std::u8string(path.begin(), path.end()));
        if (fs::is_directory(file, error))
            file /= gamedata::ManifestName;
        const uint64_t size = fs::file_size(file, error);
        if (error)
            return {};
        const auto time = fs::last_write_time(file, error);
        if (error)
            return {};
        return fmt::format("{}:{}", size, time.time_since_epoch().count());
    }

    std::string SaveDigest(const Digest& digest)
    {
        switch (digest.outcome)
        {
        case Digest::Outcome::Done: return fmt::format("done {} {} {}", digest.files, digest.bytes, digest.sha256);
        case Digest::Outcome::CutShort: return "cut " + digest.file;
        case Digest::Outcome::Unreadable: return "unreadable";
        case Digest::Outcome::Cancelled: break;
        }
        return {};
    }

    std::optional<Digest> LoadDigest(std::string_view text)
    {
        Digest digest;
        if (text == "unreadable")
            return digest;
        if (text.starts_with("cut "))
        {
            digest.outcome = Digest::Outcome::CutShort;
            digest.file = std::string(text.substr(4));
            return digest;
        }
        char sha[65] = {};
        unsigned long long files = 0, bytes = 0;
        if (!text.starts_with("done ") ||
            std::sscanf(std::string(text).c_str(), "done %llu %llu %64[0-9a-f]", &files, &bytes, sha) != 3 ||
            std::string_view(sha).size() != 64)
            return std::nullopt;
        digest.outcome = Digest::Outcome::Done;
        digest.files = files;
        digest.bytes = bytes;
        digest.sha256 = sha;
        return digest;
    }

    bool LogShowsDamagedDisc(std::string_view log)
    {
        return log.find("[bink] open failed") != std::string_view::npos ||
               log.find("[bink] invalid movie header") != std::string_view::npos;
    }

    float Job::Fraction() const
    {
        const uint64_t total = m_total;
        return total == 0 ? 0.0f : float(double(m_bytes.load()) / double(total));
    }

    void Job::Start(const std::string& path, bool cancelAtOnce)
    {
        Join();
        m_started = true;
        m_done = false;
        m_cancel = cancelAtOnce;
        m_bytes = m_total = 0;
        m_path = path;
        m_result = {};
        m_thread = std::thread([this, path] {
            m_result = ComputeDigest(path, &m_bytes, &m_total, &m_cancel);
            m_done = true;
        });
    }

    Digest Job::Collect()
    {
        Join();
        Digest result = m_result;
        m_started = m_done = false;
        return result;
    }
}
