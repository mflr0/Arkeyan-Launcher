// "Check the image" (launcher/disc_check.h) on invented XDVDFS images: the same files give the same fingerprint in two
// layouts and from a copied folder, one changed byte gives "differs", a cut image, a file that isn't a disc, Cancel, an
// edition with no reference, and the log lines that offer the check. Synthetic images in the scratch folder only.
#include "launcher/disc_check.h"
#include "launcher/sha256.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    int g_checks = 0, g_failures = 0;
#define EXPECT(cond)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        g_checks++;                                                                                                    \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            g_failures++;                                                                                              \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                                             \
        }                                                                                                              \
    } while (0)

    constexpr uint32_t Sector = 2048;
    constexpr uint64_t Xgd1Partition = 0x2080000; // one of the layouts shared/vfs.cpp reads

    struct DiscFile
    {
        std::string folder; // "" (the root) or "movies"
        std::string name;
        std::vector<uint8_t> data;
    };

    void Put16(uint8_t* p, uint16_t v) { memcpy(p, &v, 2); }
    void Put32(uint8_t* p, uint32_t v) { memcpy(p, &v, 4); }

    struct Record
    {
        std::string name;
        uint32_t sector, size;
        bool directory;
    };

    // A directory's records chained by their right subtree, 4-byte aligned.
    void WriteDirectory(uint8_t* at, const std::vector<Record>& records)
    {
        size_t offset = 0;
        for (size_t i = 0; i < records.size(); i++)
        {
            const Record& record = records[i];
            const size_t length = (14 + record.name.size() + 3) / 4 * 4;
            Put16(at + offset, 0);
            Put16(at + offset + 2, i + 1 < records.size() ? uint16_t((offset + length) / 4) : 0);
            Put32(at + offset + 4, record.sector);
            Put32(at + offset + 8, record.size);
            at[offset + 12] = record.directory ? 0x10 : 0x80;
            at[offset + 13] = uint8_t(record.name.size());
            memcpy(at + offset + 14, record.name.data(), record.name.size());
            offset += length;
        }
    }

    // The volume descriptor at sector 32 of the partition, the root at 33, "movies" at 34, the files from 35 on.
    // `keep`: the bytes written (a cut image when smaller than the whole).
    bool WriteImage(const fs::path& path, const std::vector<DiscFile>& files, uint64_t partition, uint64_t keep = 0)
    {
        std::vector<Record> root{ { "movies", 34, Sector, true } }, movies;
        uint32_t sector = 35;
        for (const DiscFile& file : files)
        {
            (file.folder.empty() ? root : movies).push_back({ file.name, sector, uint32_t(file.data.size()), false });
            sector += uint32_t((file.data.size() + Sector - 1) / Sector);
        }
        std::vector<uint8_t> image(size_t(sector) * Sector, 0);
        memcpy(&image[32 * Sector], "MICROSOFT*XBOX*MEDIA", 20);
        Put32(&image[32 * Sector + 20], 33);
        Put32(&image[32 * Sector + 24], Sector);
        WriteDirectory(&image[33 * Sector], root);
        WriteDirectory(&image[34 * Sector], movies);
        sector = 35;
        for (const DiscFile& file : files)
        {
            if (!file.data.empty())
                memcpy(&image[size_t(sector) * Sector], file.data.data(), file.data.size());
            sector += uint32_t((file.data.size() + Sector - 1) / Sector);
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.seekp(std::streamoff(partition));
        out.write(reinterpret_cast<const char*>(image.data()),
            std::streamsize(keep != 0 ? keep - partition : uint64_t(image.size())));
        return bool(out);
    }

    // The files as "Copy the game data" leaves them: the disc's tree in a folder.
    void WriteFolder(const fs::path& folder, const std::vector<DiscFile>& files)
    {
        fs::create_directories(folder / "movies");
        for (const DiscFile& file : files)
        {
            std::ofstream out(file.folder.empty() ? folder / file.name : folder / file.folder / file.name,
                std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(file.data.data()), std::streamsize(file.data.size()));
        }
        std::ofstream(folder / "gamedata.manifest") << "not one of the disc's files\n";
    }

    std::vector<uint8_t> Bytes(size_t size, uint8_t seed)
    {
        std::vector<uint8_t> data(size);
        uint32_t x = 0x9E3779B9u * (seed + 1u);
        for (uint8_t& byte : data)
        {
            x = x * 1664525u + 1013904223u;
            byte = uint8_t(x >> 24);
        }
        return data;
    }

    void Sha256Checks()
    {
        EXPECT(Sha256Hex({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        const std::string abc = "abc";
        EXPECT(Sha256Hex({ reinterpret_cast<const uint8_t*>(abc.data()), abc.size() }) ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        // The standard's long vectors: two blocks, and a million "a" in one call (thousands of blocks at once, the CPU's
        // instructions where they are used).
        const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        EXPECT(Sha256Hex({ reinterpret_cast<const uint8_t*>(two.data()), two.size() }) ==
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
        const std::vector<uint8_t> million(1000000, uint8_t('a'));
        EXPECT(Sha256Hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        // Fed in uneven pieces, across the 64-byte blocks and the padding's two-block case: the one-shot answer.
        const std::vector<uint8_t> data = Bytes(1000, 7);
        for (size_t piece : { size_t(1), size_t(55), size_t(63), size_t(64), size_t(65), size_t(333) })
        {
            Sha256 hash;
            for (size_t at = 0; at < data.size(); at += piece)
                hash.Update({ data.data() + at, std::min(piece, data.size() - at) });
            EXPECT(hash.FinishHex() == Sha256Hex(data));
        }
        for (size_t size : { size_t(55), size_t(56), size_t(64), size_t(119), size_t(120) })
        {
            Sha256 hash;
            hash.Update({ data.data(), size });
            EXPECT(hash.FinishHex() == Sha256Hex({ data.data(), size }));
        }
    }
}

int DiscCheckChecks(const fs::path& scratch)
{
    using namespace disccheck;
    printf("Disc check:\n");
    Sha256Checks();

    const fs::path folder = scratch / "disc_check";
    std::error_code error;
    fs::remove_all(folder, error);
    fs::create_directories(folder);
    const std::vector<DiscFile> files{
        { "", "default.xex", Bytes(5000, 1) },
        { "", "Data.bin", Bytes(3 * 1024 * 1024 + 17, 2) }, // more than one 1 MB block
        { "movies", "legal.bik", Bytes(70000, 3) },
        { "", "empty.txt", {} },
    };
    const uint64_t total = 5000 + 3 * 1024 * 1024 + 17 + 70000;

    // The same files in two layouts and copied in a folder: one fingerprint.
    const fs::path plain = folder / "plain.iso", partition = folder / "xgd1.iso", copy = folder / "copy";
    EXPECT(WriteImage(plain, files, 0));
    EXPECT(WriteImage(partition, files, Xgd1Partition));
    WriteFolder(copy, files);
    std::atomic<uint64_t> done{ 0 }, size{ 0 };
    const Digest good = ComputeDigest(plain.string(), &done, &size);
    EXPECT(good.outcome == Digest::Outcome::Done && good.sha256.size() == 64);
    EXPECT(good.files == 4 && good.bytes == total && done == total && size == total);
    const Digest moved = ComputeDigest(partition.string());
    EXPECT(moved.outcome == Digest::Outcome::Done && moved.sha256 == good.sha256);
    const Digest copied = ComputeDigest(copy.string());
    EXPECT(copied.outcome == Digest::Outcome::Done && copied.sha256 == good.sha256 && copied.files == 4);

    // One byte changed inside a movie: the disc reader opens it as before, the fingerprint differs.
    std::vector<DiscFile> damaged = files;
    damaged[2].data[40000] ^= 0x01;
    const fs::path bad = folder / "damaged.iso";
    EXPECT(WriteImage(bad, damaged, 0));
    const Digest changed = ComputeDigest(bad.string());
    EXPECT(changed.outcome == Digest::Outcome::Done && changed.sha256 != good.sha256);

    // A file renamed with the same bytes is another disc too.
    std::vector<DiscFile> renamed = files;
    renamed[2].name = "legal2.bik";
    EXPECT(WriteImage(folder / "renamed.iso", renamed, 0));
    EXPECT(ComputeDigest((folder / "renamed.iso").string()).sha256 != good.sha256);

    // Cut in the middle of the large file: named, never a fingerprint.
    const fs::path cut = folder / "cut.iso";
    EXPECT(WriteImage(cut, files, 0, (35 + 3) * Sector + 1024 * 1024));
    const Digest shortRead = ComputeDigest(cut.string());
    EXPECT(shortRead.outcome == Digest::Outcome::CutShort && shortRead.sha256.empty());
    EXPECT(shortRead.file == "data.bin" || shortRead.file == "movies/legal.bik");

    // Not a disc image, a missing file.
    std::ofstream(folder / "notes.txt") << "not a disc\n";
    EXPECT(ComputeDigest((folder / "notes.txt").string()).outcome == Digest::Outcome::Unreadable);
    EXPECT(ComputeDigest((folder / "missing.iso").string()).outcome == Digest::Outcome::Unreadable);

    // Cancel between two blocks.
    std::atomic<bool> cancel{ true };
    EXPECT(ComputeDigest(plain.string(), nullptr, nullptr, &cancel).outcome == Digest::Outcome::Cancelled);

    // The comparison: a release with the good copy's fingerprint, one without, a default.xex of no release.
    const std::string referenced = "1111111111111111111111111111111111111111111111111111111111111111";
    const std::string unreferenced = "2222222222222222222222222222222222222222222222222222222222222222";
    const GameRelease releases[] = {
        { "USA, Europe", 1, 2, 3, referenced.c_str(), "image", good.sha256.c_str() },
        { "Europe", 1, 4, 5, unreferenced.c_str(), "image" },
    };
    const GameInfo game{ "test", "Test", releases, "Test" };
    EXPECT(HasReference(game, referenced) && !HasReference(game, unreferenced) && !HasReference(game, "other"));
    EXPECT(ReleaseOf(game, unreferenced) == &releases[1] && ReleaseOf(game, "other") == nullptr);
    Result matches = Compare(game, referenced, moved);
    EXPECT(matches.verdict == Verdict::Matches && matches.edition == "USA, Europe");
    Result differs = Compare(game, referenced, changed);
    EXPECT(differs.verdict == Verdict::Differs && differs.edition == "USA, Europe");
    EXPECT(Compare(game, unreferenced, good).verdict == Verdict::NoReference);
    EXPECT(Compare(game, "other", good).verdict == Verdict::NoReference);
    EXPECT(Compare(game, referenced, shortRead).verdict == Verdict::CutShort);
    EXPECT(Compare(game, referenced, Digest{}).verdict == Verdict::Unreadable);
    for (Verdict verdict :
        { Verdict::Matches, Verdict::Differs, Verdict::NoReference, Verdict::CutShort, Verdict::Unreadable })
        EXPECT(!ResultText({ verdict, "USA, Europe", {} }).empty());
    EXPECT(ResultText({ Verdict::Cancelled, {}, {} }).empty());
    EXPECT(ResultText(differs).find("USA, Europe") != std::string::npos);
    // What can't be installed: a changed file, a cut image, an image the reader doesn't open.
    EXPECT(Damaged(Verdict::Differs) && Damaged(Verdict::CutShort) && Damaged(Verdict::Unreadable));
    EXPECT(!Damaged(Verdict::Matches) && !Damaged(Verdict::NoReference) && !Damaged(Verdict::Cancelled));

    // Kept in the settings and read back: the same comparison, without reading the image again.
    for (const Digest& kept : { good, shortRead, Digest{} })
    {
        std::optional<Digest> back = LoadDigest(SaveDigest(kept));
        EXPECT(back && back->outcome == kept.outcome && back->sha256 == kept.sha256 && back->file == kept.file);
        EXPECT(back && Compare(game, referenced, *back).verdict == Compare(game, referenced, kept).verdict);
    }
    EXPECT(LoadDigest(SaveDigest(good))->files == good.files && LoadDigest(SaveDigest(good))->bytes == good.bytes);
    Digest cancelled;
    cancelled.outcome = Digest::Outcome::Cancelled;
    EXPECT(SaveDigest(cancelled).empty());
    EXPECT(!LoadDigest("") && !LoadDigest("done 1 2 xyz") && !LoadDigest("other"));
    // The stamp: the same file, the same stamp; another size, another; a copy folder's is its manifest's.
    const std::string stamp = Stamp(plain.string());
    EXPECT(!stamp.empty() && Stamp(plain.string()) == stamp && Stamp(cut.string()) != stamp);
    EXPECT(!Stamp(copy.string()).empty() && Stamp((scratch / "no-such-file").string()).empty());

    // On a worker thread: the digest, then Cancel at once.
    Job job;
    job.Start(bad.string());
    EXPECT(job.Running() && job.Path() == bad.string());
    Digest threaded = job.Collect();
    EXPECT(Compare(game, referenced, threaded).verdict == Verdict::Differs && !job.Running());
    job.Start(copy.string());
    EXPECT(Compare(game, referenced, job.Collect()).verdict == Verdict::Matches);
    job.Start(plain.string(), true);
    EXPECT(job.Collect().outcome == Digest::Outcome::Cancelled);

    // The game's log: movies that didn't open offer the check; a log without them does not.
    EXPECT(LogShowsDamagedDisc("[boot] ok\n[bink] open failed game:\\movies\\legal.bik (no handle)\n"));
    EXPECT(LogShowsDamagedDisc("[bink] invalid movie header xpeclogo.bik\n"));
    EXPECT(!LogShowsDamagedDisc("[bink] opened legal.bik 1280x720\n[boot] menu\n"));
    EXPECT(!LogShowsDamagedDisc(""));

    fs::remove_all(folder, error);
    printf("  %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
