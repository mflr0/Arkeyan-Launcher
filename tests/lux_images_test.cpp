// The Lux picture reader (shared/art/lux_images.cpp) on a package cut inside
// its SYHP chunk.
#include "shared/art/lux_images.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    int g_checks = 0, g_failures = 0;
#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        g_checks++;                                                                                                    \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            g_failures++;                                                                                              \
            printf("FAIL line %d: %s\n", __LINE__, #condition);                                                        \
        }                                                                                                              \
    } while (0)

    void PutBe32(std::vector<uint8_t>& out, size_t at, uint32_t value)
    {
        if (out.size() < at + 4)
            out.resize(at + 4);
        out[at] = uint8_t(value >> 24), out[at + 1] = uint8_t(value >> 16), out[at + 2] = uint8_t(value >> 8),
        out[at + 3] = uint8_t(value);
    }
} // namespace

int LuxImageChecks()
{
    g_checks = g_failures = 0;
    const uint8_t Flushes[8] = { 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00 };
    // A package whose SYHP chunk is cut by the end of its body (offset and size
    // would read past it): skipped. The bytes after the record are what an old
    // reader took for the chunk's fields.
    {
        std::vector<uint8_t> shortBody(0x100, 0);
        memcpy(shortBody.data(), "APKF", 4);
        memcpy(&shortBody[0x20], "\0XET", 4);
        memcpy(&shortBody[0x20 + 0x30], "toy_short_hud", 13);
        memcpy(&shortBody[0x60], Flushes, 8);
        PutBe32(shortBody, 0x68, 0x80000002);
        PutBe32(shortBody, 0x6C, 20 | 1 << 6);
        PutBe32(shortBody, 0x70, (8 - 1) | (4 - 1) << 13);
        PutBe32(shortBody, 0x7C, 1 << 9);
        memcpy(&shortBody[0xF0], "SYHP", 4);
        std::vector<uint8_t> cut(8, 0);
        std::vector<uint8_t> cutHead(16, 0);
        PutBe32(cutHead, 12, uint32_t(shortBody.size()));
        cut.insert(cut.end(), cutHead.begin(), cutHead.end());
        cut.insert(cut.end(), shortBody.begin(), shortBody.end());
        PutBe32(cut, cut.size(),
            0x80); // where the size field of the chunk would be
        int cutFound = 0;
        art::lux::DecodeImages(
            cut, [](const std::string&) { return true; }, [&](const std::string&, const art::Image&) { cutFound++; });
        CHECK(cutFound == 0);
        // The same package with the chunk whole is read: the test is about the cut,
        // not the package.
        std::vector<uint8_t> whole = shortBody;
        whole.resize(0x110, 0);
        PutBe32(whole, 0xF0 + 8, 0), PutBe32(whole, 0xF0 + 16, 0x80);
        std::vector<uint8_t> full(8, 0);
        PutBe32(cutHead, 12, uint32_t(whole.size()));
        full.insert(full.end(), cutHead.begin(), cutHead.end());
        full.insert(full.end(), whole.begin(), whole.end());
        art::lux::DecodeImages(
            full, [](const std::string&) { return true; }, [&](const std::string&, const art::Image&) { cutFound++; });
        CHECK(cutFound == 1);
    }
    printf("Lux pictures: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}
