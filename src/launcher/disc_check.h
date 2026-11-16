#pragma once

// The image check (docs/install.md, "Image check"): once a disc is verified, every file is read and hashed, and compared
// with the known good copy of its edition (GameRelease::contentSha256). The disc check before (CheckGameSource) reads
// default.xex only, and a damaged image can pass it and then hang a game at its loading screen (issue #64). A damaged
// image can't be installed. No UI: ArkeyanLauncherTest runs these on invented images.
#include "launcher/launcher.h"
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace disccheck
{
    // The fingerprint of a disc's contents, not of the image file: one SHA-256 over every file, sorted by its lowercase
    // path, each as "<path>\0<size>\0<SHA-256 of its bytes>\n". A good disc gives the same one whatever the image's layout
    // (the whole disc, its game partition alone, a trimmed image) and from its copied game data; one changed byte of any
    // file gives another. Files are read in 1 MB blocks, several at once (one per core but one, eight at most).
    struct Digest
    {
        enum class Outcome
        {
            Done,
            Cancelled,
            Unreadable, // not a disc image the reader opens
            CutShort,   // a file ends past the end of the image, or its bytes can't be read
        };
        Outcome outcome = Outcome::Unreadable;
        std::string sha256; // Done only
        uint64_t files = 0, bytes = 0;
        std::string file; // CutShort: the file that couldn't be read whole
    };
    // `done` and `total` (when set) receive the bytes read and to read; `cancel` stops between two blocks.
    Digest ComputeDigest(const std::string& path, std::atomic<uint64_t>* done = nullptr,
        std::atomic<uint64_t>* total = nullptr, const std::atomic<bool>* cancel = nullptr);

    // The release whose default.xex is this one (CheckGameSource's xexSha256), if any. Another edition with the same game
    // code (matched by imageSha256 alone) is not that release, and its files differ: it has no reference.
    const GameRelease* ReleaseOf(const GameInfo& game, std::string_view xexSha256);
    // Whether the disc of that default.xex has a known good copy to compare with.
    bool HasReference(const GameInfo& game, std::string_view xexSha256);

    enum class Verdict
    {
        Matches,
        Differs,
        NoReference,
        CutShort,
        Unreadable,
        Cancelled,
    };
    // Differs, CutShort, Unreadable: the image can't be installed (or copied).
    bool Damaged(Verdict verdict);
    struct Result
    {
        Verdict verdict = Verdict::Unreadable;
        std::string edition; // the release's label (Matches, Differs)
        std::string digest;  // what was computed, when it was
    };
    // NoReference: every file was read, with nothing to compare it to.
    Result Compare(const GameInfo& game, std::string_view xexSha256, const Digest& digest);
    // The result in one line for the player, in the launcher's language; empty for Cancelled.
    std::string ResultText(const Result& result);

    // A digest is kept in the settings (the game's section) with the stamp of what was read, so the image is not read
    // again at each start; it is compared again each time, as a later launcher may know a good copy this one doesn't.
    // Stamp: the file's size and modification time; a copy folder's: its manifest's. Empty when it can't be read.
    std::string Stamp(const std::string& path);
    std::string SaveDigest(const Digest& digest); // empty for Cancelled
    std::optional<Digest> LoadDigest(std::string_view text);

    // Whether a game's log says its movies didn't open (runtime/d3d/bink_display.cpp: "[bink] open failed", "[bink]
    // invalid movie header"): what a damaged image did in issue #64, with no failed disc read logged.
    bool LogShowsDamagedDisc(std::string_view log);

    // The check on a worker thread, like GameCopyJob.
    class Job
    {
    public:
        ~Job()
        {
            Cancel();
            Join();
        }
        bool Running() const { return m_started; } // started and not yet collected
        bool Done() const { return m_done; }
        void Start(const std::string& path, bool cancelAtOnce = false /* tests */);
        void Cancel() { m_cancel = true; }
        float Fraction() const;
        uint64_t Bytes() const { return m_bytes; }
        uint64_t Total() const { return m_total; }
        const std::string& Path() const { return m_path; }
        // Waits for the end and returns the digest; the job can start again.
        Digest Collect();

    private:
        void Join()
        {
            if (m_thread.joinable())
                m_thread.join();
        }

        std::thread m_thread;
        std::atomic<bool> m_started{ false }, m_done{ false }, m_cancel{ false };
        std::atomic<uint64_t> m_bytes{ 0 }, m_total{ 0 };
        std::string m_path;
        Digest m_result;
    };
}
