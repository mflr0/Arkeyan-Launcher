// The portal status: the VID:PID table, the line the launcher shows, and the background watcher (portal_status.h).
#include "launcher/portal_status.h"
#include "launcher/i18n.h"
#include <SDL3/SDL.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#elif defined(_WIN32)
#include <windows.h>
#include <cfgmgr32.h>
#ifndef CM_GETIDLIST_FILTER_PRESENT
#define CM_GETIDLIST_FILTER_PRESENT 0x00000100 // Windows 7 and later; older mingw-w64 headers lack it
#endif
#endif

namespace launcher_portal
{
    namespace
    {
        struct Model
        {
            uint16_t vendorId, productId;
            const char* name;
        };

        constexpr Model Models[] = {
            { Activision, PortalOfPower, "Portal of Power" },
            { Activision, XboxPortal, "Xbox 360 portal" },
            { Activision, XboxOnePortal, "Xbox One portal" },
            { Activision, 0x09AB, "Xbox One portal" }, // Santroller's capture of another Xbox One portal
            { OtherXboxVendor, OtherXboxPortal, "Xbox 360 portal" },
        };

        constexpr uint64_t ScanIntervalMs = 2000;
        constexpr uint64_t PollIntervalMs = 250; // how often the change count is read, and how quickly Stop returns
    }

    std::string ModelName(uint16_t vendorId, uint16_t productId)
    {
        for (const Model& model : Models)
            if (model.vendorId == vendorId && model.productId == productId)
                return model.name;
        if (vendorId != Activision)
            return {};
        char name[64];
        snprintf(name, sizeof(name), "Skylanders portal (%04X:%04X)", vendorId, productId);
        return name;
    }

    std::string Status::Line() const
    {
        if (!available)
            return {};
        if (connected)
            return Tf("Portal: {}", model);
        return Tr("Portal: not detected");
    }

    Status MakeStatus(const Scan& scan)
    {
        Status status;
        status.available = scan.available;
        if (!scan.available)
            return status;
        // The first portal of the list: the launcher only names one, even with several plugged in.
        for (const Device& device : scan.devices)
            if (std::string model = ModelName(device.vendorId, device.productId); !model.empty())
            {
                status.connected = true;
                status.model = std::move(model);
                break;
            }
        return status;
    }

    Status Query(const Enumerator& enumerate) { return MakeStatus(enumerate()); }

    Scan EnumerateHid()
    {
        // All HID devices, not only game controllers (SDL's default); done once, before the first enumeration.
        static const bool ready = [] {
            SDL_SetHint(SDL_HINT_HIDAPI_ENUMERATE_ONLY_CONTROLLERS, "0");
            return SDL_hid_init() == 0;
        }();
        Scan scan;
        if (!ready)
            return scan;
        scan.available = true;
        SDL_hid_device_info* devices = SDL_hid_enumerate(Activision, 0);
        for (SDL_hid_device_info* device = devices; device != nullptr; device = device->next)
            scan.devices.push_back({ device->vendor_id, device->product_id });
        SDL_hid_free_enumeration(devices);
        return scan;
    }

    Scan Merge(const Scan& hid, const Scan& usb)
    {
        Scan merged = hid;
        merged.available = hid.available || usb.available;
        for (const Device& device : usb.devices)
        {
            bool known = false;
            for (const Device& seen : merged.devices)
                known = known || (seen.vendorId == device.vendorId && seen.productId == device.productId);
            if (!known)
                merged.devices.push_back(device);
        }
        return merged;
    }

    Scan ListSysfsUsb(const std::string& root)
    {
        Scan scan;
        std::error_code error;
        std::filesystem::directory_iterator folders(root, error);
        if (error)
            return scan;
        scan.available = true;
        auto hex = [](const std::filesystem::path& file) {
            std::ifstream in(file);
            std::string text;
            if (!(in >> text) || text.size() != 4)
                return -1;
            char* end = nullptr;
            long value = strtol(text.c_str(), &end, 16);
            return *end == '\0' ? int(value) : -1;
        };
        for (const auto& folder : folders)
        {
            const int vendor = hex(folder.path() / "idVendor"), product = hex(folder.path() / "idProduct");
            if (vendor >= 0 && product >= 0)
                scan.devices.push_back({ uint16_t(vendor), uint16_t(product) });
        }
        return scan;
    }

    bool ParseWindowsUsbId(const std::string& id, Device& device)
    {
        std::string upper = id;
        for (char& c : upper)
            c = char(toupper(static_cast<unsigned char>(c)));
        auto field = [&](const char* name, uint16_t& value) {
            size_t at = upper.find(name);
            if (at == std::string::npos || at + strlen(name) + 4 > upper.size())
                return false;
            const std::string digits = upper.substr(at + strlen(name), 4);
            if (digits.find_first_not_of("0123456789ABCDEF") != std::string::npos)
                return false;
            value = uint16_t(strtoul(digits.c_str(), nullptr, 16));
            return true;
        };
        return upper.rfind("USB\\", 0) == 0 && field("VID_", device.vendorId) && field("PID_", device.productId);
    }

    Scan EnumerateUsb()
    {
#if defined(__linux__)
        return ListSysfsUsb("/sys/bus/usb/devices");
#elif defined(__APPLE__)
        Scan scan;
        io_iterator_t iterator = 0;
        if (IOServiceGetMatchingServices(MACH_PORT_NULL, IOServiceMatching("IOUSBHostDevice"), &iterator) !=
            KERN_SUCCESS)
            return scan;
        scan.available = true;
        while (io_object_t service = IOIteratorNext(iterator))
        {
            auto number = [&](CFStringRef key) {
                int value = 0;
                if (CFTypeRef property = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0))
                {
                    if (CFGetTypeID(property) == CFNumberGetTypeID())
                        CFNumberGetValue(static_cast<CFNumberRef>(property), kCFNumberIntType, &value);
                    CFRelease(property);
                }
                return value;
            };
            scan.devices.push_back({ uint16_t(number(CFSTR("idVendor"))), uint16_t(number(CFSTR("idProduct"))) });
            IOObjectRelease(service);
        }
        IOObjectRelease(iterator);
        return scan;
#elif defined(_WIN32)
        Scan scan;
        const ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
        std::vector<char> list;
        for (int attempt = 0; attempt < 3; attempt++) // a device plugged in between the two calls makes the list longer
        {
            ULONG size = 0;
            if (CM_Get_Device_ID_List_SizeA(&size, "USB", flags) != CR_SUCCESS || size == 0)
                return scan;
            list.assign(size, '\0');
            CONFIGRET result = CM_Get_Device_ID_ListA("USB", list.data(), size, flags);
            if (result == CR_SUCCESS)
                break;
            if (result != CR_BUFFER_SMALL || attempt == 2)
                return scan;
        }
        scan.available = true;
        for (const char* id = list.data(); id < list.data() + list.size() && *id != '\0'; id += strlen(id) + 1)
            if (Device device; ParseWindowsUsbId(id, device))
                scan.devices.push_back(device);
        return scan;
#else
        return {};
#endif
    }

    void Watcher::Start()
    {
        if (m_thread.joinable())
            return;
        m_stop = false;
        m_thread = std::thread([this] { Run(); });
    }

    void Watcher::Stop()
    {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
    }

    Status Watcher::Current() const
    {
        std::lock_guard lock(m_mutex);
        return m_status;
    }

    void Watcher::Run()
    {
        Scan hid = EnumerateHid(), usb = EnumerateUsb();
        {
            std::lock_guard lock(m_mutex);
            m_status = MakeStatus(Merge(hid, usb));
            if (!hid.available && !usb.available)
                return; // neither list can be read: no error, and no point polling
        }

        // HID again when SDL counts a change; USB every 2 s (no change count covers it: reading the list is cheap).
        uint32_t changes = hid.available ? SDL_hid_device_change_count() : 0;
        uint64_t hidAt = SDL_GetTicks(), usbAt = hidAt;
        while (!m_stop)
        {
            SDL_Delay(PollIntervalMs);
            const uint64_t at = SDL_GetTicks();
            bool changed = false;
            if (hid.available && at - hidAt >= ScanIntervalMs)
            {
                if (uint32_t now = SDL_hid_device_change_count(); now != changes)
                {
                    changes = now;
                    hidAt = at;
                    hid = EnumerateHid();
                    changed = true;
                }
            }
            if (usb.available && at - usbAt >= ScanIntervalMs)
            {
                usbAt = at;
                usb = EnumerateUsb();
                changed = true;
            }
            if (!changed)
                continue;
            Status status = MakeStatus(Merge(hid, usb));
            std::lock_guard lock(m_mutex);
            m_status = std::move(status);
        }
    }
}
