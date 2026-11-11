// Copied portal diagnostics with synthetic text only: no device or process.
#include "launcher/game_run.h"
#include "launcher/portal_probe_text.h"
#include <cstdio>
#include <string>

namespace
{
    int g_portalTextFailures = 0;

#define PORTAL_EXPECT(condition)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition);                                  \
            g_portalTextFailures++;                                                                                    \
        }                                                                                                              \
    } while (0)

    PortalProbeEntry WorkingPortal()
    {
        PortalProbeEntry entry;
        entry.kind = "hid";
        entry.vid = "1430";
        entry.pid = "0150";
        entry.product = "Test portal";
        entry.opened = true;
        entry.status = "ok";
        entry.id = "test-id";
        entry.model = "Test model";
        entry.verdict = "works";
        entry.write = "output-report";
        entry.details = "test interface; test driver";
        entry.methods = { { "output-report", "R: ok; S: ok" },
            { "hid-write", "R: error: write failed (hid_write/WaitForSingleObject: (0x000003E5) "
                           "Overlapped I/O operation is in progress.); S: not tried" } };
        return entry;
    }

    void CopiedLines()
    {
        PortalProbeEntry entry = WorkingPortal();
        const std::string expected = "hid 1430:0150 \"Test portal\": opened, ok\n"
                                     "  model: test-id Test model; Spyro's Adventure: works\n"
                                     "  device: test interface\n"
                                     "  test driver\n"
                                     "  output-report: R: ok; S: ok\n"
                                     "  hid-write: not needed (output-report works)\n"
                                     "  the game writes with output-report\n";
        PORTAL_EXPECT(PortalProbeEntryText(entry) == expected);
        PORTAL_EXPECT(
            entry.methods[1].second.find("0x000003E5") != std::string::npos); // reporting leaves evidence intact

        // A damaged selected label must not reach Copy. The successful method has an intact, owned name.
        entry.write = "output-" + std::string("\1\0", 2) + "invalid";
        PORTAL_EXPECT(PortalProbeEntryText(entry) == expected);
        entry.write = "output-u0001u0000invalid";
        PORTAL_EXPECT(PortalProbeEntryText(entry) == expected);

        // Keep the completed copy after every source string has been destroyed and other strings reuse storage.
        std::string copied;
        {
            PortalProbeEntry temporary = WorkingPortal();
            copied = PortalProbeEntryText(temporary);
        }
        const std::string overwrite(8192, 'x');
        PORTAL_EXPECT(copied == expected && overwrite.size() == 8192);

        entry.verdict = "warns";
        entry.verdictReason = "test reason";
        PORTAL_EXPECT(
            PortalProbeEntryText(entry).find("Spyro's Adventure: warns (test reason)\n") != std::string::npos);
        entry.verdictReason = "test reason (additional detail)";
        PORTAL_EXPECT(
            PortalProbeEntryText(entry).find("warns (test reason (additional detail))\n") != std::string::npos);
        entry.product = "Test \"portal\" \xc3\xa9";
        PORTAL_EXPECT(
            PortalProbeEntryText(entry).find("\"Test \"portal\" \xc3\xa9\": opened, ok\n") != std::string::npos);

        PortalProbeEntry older;
        older.kind = "usb";
        older.vid = "1430";
        older.pid = "1f17";
        older.product = "Test portal";
        older.status = "error: not opened";
        PORTAL_EXPECT(PortalProbeEntryText(older) == "usb 1430:1f17 \"Test portal\": not opened, error: not opened\n");
        older.write = "hid-write";
        PORTAL_EXPECT(PortalProbeEntryText(older).ends_with("  the game writes with hid-write\n"));
    }

    void PendingWrites()
    {
        // Recognize the Windows code independently of the host OS, zero padding, hex case or localized message.
        for (const char* code : { "3E5", "000003e5" })
        {
            PortalProbeEntry entry = WorkingPortal();
            entry.methods[1].second = std::string("R: error: write failed (hid_write/WaitForSingleObject: (0x") + code +
                                      ") test message); S: not tried";
            PORTAL_EXPECT(PortalProbeEntryText(entry).find("  hid-write: not needed (output-report works)\n") !=
                          std::string::npos);
        }
        // Real failures, malformed codes and failures after the pending write remain visible.
        for (const char* line :
            { "R: error: write failed (hid_write/WaitForSingleObject: (0x00000005) denied); S: not tried",
                "R: error: write failed (hid_write/WaitForSingleObject: (0x3E5junk) test); S: not tried",
                "R: error: write failed (hid_write/WaitForSingleObject: (0x1000003E5) test); S: not tried",
                "R: error: write failed (hid_write/WaitForSingleObject: (0x3E5) test); S: error: test failure",
                "R: error: write failed (other backend: (0x3E5) test); S: not tried",
                "R: error: test failure; S: not tried" })
        {
            PortalProbeEntry entry = WorkingPortal();
            entry.methods[1].second = line;
            PORTAL_EXPECT(
                PortalProbeEntryText(entry).find(std::string("  hid-write: ") + line + "\n") != std::string::npos);
        }
        for (const char* output : { "R: error: test failure; S: not tried", "R: ok; S: error: test failure",
                 "R: okay; S: ok", "R: ok; S: okay", "R: ok", "" })
        {
            PortalProbeEntry entry = WorkingPortal();
            entry.methods[0].second = output;
            PORTAL_EXPECT(PortalProbeEntryText(entry).find("  hid-write: R: error: write failed") != std::string::npos);
        }
        PortalProbeEntry entry = WorkingPortal();
        entry.methods.erase(entry.methods.begin());
        PORTAL_EXPECT(PortalProbeEntryText(entry).find("  hid-write: R: error: write failed") != std::string::npos);
        entry = WorkingPortal();
        entry.status = "error: test failure";
        PORTAL_EXPECT(PortalProbeEntryText(entry).find("  hid-write: R: error: write failed") != std::string::npos);
        entry = WorkingPortal();
        entry.opened = false;
        PORTAL_EXPECT(PortalProbeEntryText(entry).find("  hid-write: R: error: write failed") != std::string::npos);
    }

    void SelectedMethod()
    {
        for (const char* method : { "output-report", "hid-write", "usb-control" })
        {
            PortalProbeEntry entry = WorkingPortal();
            entry.write = method;
            PORTAL_EXPECT(
                PortalProbeEntryText(entry).ends_with(std::string("  the game writes with ") + method + "\n"));
        }
        PortalProbeEntry entry = WorkingPortal();
        entry.write.clear();
        PORTAL_EXPECT(PortalProbeEntryText(entry).find("the game writes with") == std::string::npos);
        entry.write = "damaged";
        entry.methods[1].second = "R: ok, test response; S: ok, test response";
        PORTAL_EXPECT(PortalProbeEntryText(entry).find("the game writes with") == std::string::npos); // ambiguous
        entry.methods[0].second = "R: error: test failure; S: not tried";
        PORTAL_EXPECT(PortalProbeEntryText(entry).ends_with("  the game writes with hid-write\n"));
        entry.methods[1].second = "R: error: test failure; S: not tried";
        PORTAL_EXPECT(PortalProbeEntryText(entry).find("the game writes with") == std::string::npos);
        entry = WorkingPortal();
        entry.write = "damaged";
        entry.opened = false;
        PORTAL_EXPECT(PortalProbeEntryText(entry).find("the game writes with") == std::string::npos);
        entry = WorkingPortal();
        entry.write = "damaged";
        entry.status = "error: test failure";
        PORTAL_EXPECT(PortalProbeEntryText(entry).find("the game writes with") == std::string::npos);
    }
}

int PortalTextChecks()
{
    g_portalTextFailures = 0;
    CopiedLines();
    PendingWrites();
    SelectedMethod();
    return g_portalTextFailures;
}

#if defined(PORTAL_TEXT_TEST_STANDALONE)
int main()
{
    const int failures = PortalTextChecks();
    printf(failures == 0 ? "All portal text checks passed\n" : "%d portal text check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
#endif
