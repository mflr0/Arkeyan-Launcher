#pragma once

// Whether a real Portal of Power is plugged in: the VID:PID table and the status line ("Portal: <model>" or
// "Portal: not detected"), then a watcher on a worker thread that re-enumerates SDL's HID devices when one is plugged in or
// out, and the system's USB devices every 2 s: Xbox portals are not HID devices (docs/support.md). Nothing is opened,
// so a game using the portal is never disturbed. The watcher is started by the launcher window; ArkeyanLauncherTest
// checks the table, the status logic with fake enumerators and the USB lists' parsers.
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace launcher_portal
{
    inline constexpr uint16_t Activision = 0x1430; // every Skylanders portal's vendor
    inline constexpr uint16_t PortalOfPower = 0x0150; // Wii, PS3, Wii U, PC, Mac
    inline constexpr uint16_t XboxPortal = 0x1F17; // Xbox 360, a vendor-specific USB device
    inline constexpr uint16_t XboxOnePortal = 0x09AA; // Xbox One (Trap Team's), a GIP device; Windows: "Xbox Gaming Device"
    // In Xenia's table of Xbox 360 portals; no portal seen here has had it.
    inline constexpr uint16_t OtherXboxVendor = 0x24C6;
    inline constexpr uint16_t OtherXboxPortal = 0xFA00;

    struct Device
    {
        uint16_t vendorId = 0;
        uint16_t productId = 0;
    };

    // One enumeration: `available` is false when SDL's HID API could not be started (then there is nothing to show).
    struct Scan
    {
        bool available = false;
        std::vector<Device> devices;
    };
    using Enumerator = std::function<Scan()>;

    // The model for a portal's ids, else "Skylanders portal (1430:XXXX)" for another Activision product, else empty.
    std::string ModelName(uint16_t vendorId, uint16_t productId);

    struct Status
    {
        bool available = false;
        bool connected = false;
        std::string model;
        // The line to show; empty when HID is unavailable (no error) or while the first scan runs.
        std::string Line() const;
    };

    Status MakeStatus(const Scan& scan);
    Status Query(const Enumerator& enumerate); // the status logic, with a fake enumerator in tests

    // SDL_hid_init and SDL_hid_enumerate(0x1430, 0), which can take tens of milliseconds: a worker thread only.
    Scan EnumerateHid();
    // The USB devices the system lists, without opening any: Linux's /sys/bus/usb/devices, macOS's IOKit registry,
    // Windows' configuration manager. `available` is false where there is no such list.
    Scan EnumerateUsb();
    // The parsers behind it, for the tests: one Linux device folder's idVendor and idProduct files (`root` is
    // /sys/bus/usb/devices or a fake), and a Windows device instance id ("USB\\VID_1430&PID_1F17\\...").
    Scan ListSysfsUsb(const std::string& root);
    bool ParseWindowsUsbId(const std::string& id, Device& device);
    // HID first, then USB; a device both list is named once.
    Scan Merge(const Scan& hid, const Scan& usb);

    // Scans once at start, then only when SDL_hid_device_change_count() has changed, at most every 2 s. Current() never
    // blocks the UI.
    class Watcher
    {
    public:
        Watcher() = default;
        ~Watcher() { Stop(); }
        Watcher(const Watcher&) = delete;
        Watcher& operator=(const Watcher&) = delete;

        void Start();
        void Stop();
        Status Current() const;

    private:
        void Run();

        mutable std::mutex m_mutex;
        Status m_status;
        std::thread m_thread;
        std::atomic<bool> m_stop{ false };
    };
}
