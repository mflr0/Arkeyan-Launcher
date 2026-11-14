// ArkeyanLauncherTest's checks for one launcher per data folder (launcher/single_instance.h): the lock, the request a
// second start leaves, the self-update's wait. Scratch folders only. Called from launcher_test.cpp's main; returns the
// number of failures.
#include "launcher/launcher.h"
#include "launcher/single_instance.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace
{
    namespace fs = std::filesystem;
    using namespace single_instance;

    int g_instanceFailures = 0;

#define INSTANCE_EXPECT(condition)                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition);                                  \
            g_instanceFailures++;                                                                                      \
        }                                                                                                              \
    } while (0)

    void WriteRequest(const fs::path& root, const char* text)
    {
        std::ofstream(RequestFile(root), std::ios::binary) << text;
    }

    long long MsSince(std::chrono::steady_clock::time_point start)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    }
}

int SingleInstanceChecks(const fs::path& scratch)
{
    fs::remove_all(scratch);
    const fs::path one = scratch / "data", two = scratch / "portable" / "data";

    // The lock: once per data root, another root beside it, free again once released.
    {
        Lock first, second, other;
        INSTANCE_EXPECT(first.Acquire(one) && first.Held() && fs::exists(LockFile(one)));
        INSTANCE_EXPECT(!second.Acquire(one) && !second.Held());
        INSTANCE_EXPECT(other.Acquire(two));
#if defined(_WIN32)
        INSTANCE_EXPECT(HolderPid(one) == uint32_t(_getpid()));
#else
        INSTANCE_EXPECT(HolderPid(one) == uint32_t(getpid()));
#endif
        first.Release();
        INSTANCE_EXPECT(!first.Held() && fs::exists(LockFile(one))); // the file stays: only the lock counts
        INSTANCE_EXPECT(second.Acquire(one));
    }
    {
        Lock again; // every Lock above was destroyed: released
        INSTANCE_EXPECT(again.Acquire(one) && again.Acquire(one)); // taken again by the same Lock: released first
    }

    // The request: read once, then gone; anything unknown removed and ignored.
    {
        INSTANCE_EXPECT(TakeRequest(one).kind == Request::Kind::None);
        INSTANCE_EXPECT(SendRequest(one, { Request::Kind::Focus, "" }));
        INSTANCE_EXPECT(TakeRequest(one).kind == Request::Kind::Focus);
        INSTANCE_EXPECT(TakeRequest(one).kind == Request::Kind::None && !fs::exists(RequestFile(one)));
        INSTANCE_EXPECT(SendRequest(one, { Request::Kind::Play, "giants" }));
        const Request play = TakeRequest(one);
        INSTANCE_EXPECT(play.kind == Request::Kind::Play && play.game == "giants");
        INSTANCE_EXPECT(!SendRequest(one, { Request::Kind::Play, "nosuchgame" }) && !fs::exists(RequestFile(one)));
        INSTANCE_EXPECT(!SendRequest(one, { Request::Kind::None, "" }));
        for (const char* bad :
            { "play nosuchgame\n", "play ../ssa", "play", "focus please", "", "PLAY ssa", "play ssa giants" })
        {
            WriteRequest(one, bad);
            INSTANCE_EXPECT(TakeRequest(one).kind == Request::Kind::None && !fs::exists(RequestFile(one)));
        }
        WriteRequest(one, "play ssa\r\n");
        INSTANCE_EXPECT(TakeRequest(one).game == "ssa");
        // A request the last launcher never read is removed by the next one to take the lock.
        WriteRequest(one, "focus\n");
        Lock next;
        INSTANCE_EXPECT(next.Acquire(one) && !fs::exists(RequestFile(one)));
    }

    // The self-update: the new launcher waits for the old one to close; it gives up when it doesn't.
    {
        Lock old, fresh;
        INSTANCE_EXPECT(old.Acquire(one));
        auto start = std::chrono::steady_clock::now();
        INSTANCE_EXPECT(!fresh.Acquire(one, 300));
        INSTANCE_EXPECT(MsSince(start) >= 290);
        std::thread closing([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            old.Release();
        });
        start = std::chrono::steady_clock::now();
        const bool took = fresh.Acquire(one, 5000);
        const long long waited = MsSince(start);
        closing.join();
        INSTANCE_EXPECT(took && waited >= 350 && waited < 3000);
    }

    // A second start: the holder takes its request; when the holder ends first, the second start runs instead.
    {
        Lock holder, second;
        INSTANCE_EXPECT(holder.Acquire(one));
        std::atomic<bool> got{ false };
        std::thread polling([&] {
            for (int i = 0; i < 100 && !got; i++)
            {
                const Request request = TakeRequest(one);
                got = request.kind == Request::Kind::Play && request.game == "ssa";
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        });
        INSTANCE_EXPECT(HandOver(second, one, { Request::Kind::Play, "ssa" }));
        polling.join();
        INSTANCE_EXPECT(got && !second.Held());
        std::thread ending([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            holder.Release();
        });
        INSTANCE_EXPECT(!HandOver(second, one, { Request::Kind::Focus, "" }));
        ending.join();
        INSTANCE_EXPECT(second.Held() && !fs::exists(RequestFile(one)));
        // A holder that never reads (a modal dialog): the second start still ends, the request left for later.
        Lock third;
        const auto start = std::chrono::steady_clock::now();
        INSTANCE_EXPECT(HandOver(third, one, { Request::Kind::Focus, "" }, 300) && !third.Held());
        INSTANCE_EXPECT(MsSince(start) < 2000 && TakeRequest(one).kind == Request::Kind::Focus);
    }

    printf("One launcher per data folder: lock, request, hand-over and the self-update's wait: %d failures\n",
        g_instanceFailures);
    return g_instanceFailures;
}
