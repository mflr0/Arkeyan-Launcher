#include "launcher/portal_probe_text.h"
#include "launcher/game_run.h"
#include <charconv>
#include <string_view>

namespace
{
    bool CommandOk(std::string_view line, std::string_view command)
    {
        if (!line.starts_with(command))
            return false;
        return line.size() == command.size() || line[command.size()] == ',' || line[command.size()] == ';';
    }

    bool MethodWorks(std::string_view line)
    {
        const size_t next = line.find("; S: ");
        return CommandOk(line, "R: ok") && next != std::string_view::npos && CommandOk(line.substr(next + 2), "S: ok");
    }

    bool KnownMethod(std::string_view method)
    {
        return method == "output-report" || method == "hid-write" || method == "usb-control";
    }

    std::string WriteMethod(const PortalProbeEntry& entry)
    {
        if (entry.write.empty() || KnownMethod(entry.write))
            return entry.write;
        // The runtime supplies the selected label. If it is damaged, only an unambiguous successful method can
        // recover it; copy the method's owned name, never a pointer or a slice of the damaged label.
        if (!entry.opened || entry.status != "ok")
            return {};
        std::string recovered;
        for (const auto& [method, line] : entry.methods)
        {
            if (!KnownMethod(method) || !MethodWorks(line))
                continue;
            if (!recovered.empty())
                return {};
            recovered = method;
        }
        return recovered;
    }

    bool PendingHidWrite(std::string_view line)
    {
        if (!line.starts_with("R: error: write failed (hid_write/") || !line.ends_with("); S: not tried"))
            return false;
        const size_t start = line.find("(0x"), end = line.find(')', start);
        if (start == std::string_view::npos || end == std::string_view::npos)
            return false;
        unsigned code = 0;
        const auto parsed = std::from_chars(line.data() + start + 3, line.data() + end, code, 16);
        return parsed.ec == std::errc() && parsed.ptr == line.data() + end && code == 0x3E5; // ERROR_IO_PENDING
    }
}

std::string PortalProbeEntryText(const PortalProbeEntry& entry)
{
    std::string text = entry.kind + " " + entry.vid + ":" + entry.pid + " \"" + entry.product +
                       "\": " + (entry.opened ? "opened" : "not opened") + ", " + entry.status + "\n";
    if (!entry.id.empty())
    {
        text += "  model: " + entry.id + " " + entry.model + "; Spyro's Adventure: " + entry.verdict;
        if (!entry.verdictReason.empty())
            text += " (" + entry.verdictReason + ")";
        text += '\n';
    }
    // The runtime's details are "; "-separated: the device's interfaces and driver, then each step and packet.
    for (size_t at = 0; at < entry.details.size();)
    {
        size_t end = entry.details.find("; ", at);
        if (end == std::string::npos)
            end = entry.details.size();
        text += "  " + std::string(at == 0 ? "device: " : "") + entry.details.substr(at, end - at) + "\n";
        at = end + 2;
    }
    bool outputWorks = false;
    if (entry.kind == "hid" && entry.opened && entry.status == "ok")
        for (const auto& [method, line] : entry.methods)
            outputWorks = outputWorks || (method == "output-report" && MethodWorks(line));
    for (const auto& [method, line] : entry.methods)
    {
        text += "  " + method + ": ";
        text +=
            outputWorks && method == "hid-write" && PendingHidWrite(line) ? "not needed (output-report works)" : line;
        text += '\n';
    }
    if (const std::string method = WriteMethod(entry); !method.empty())
        text += "  the game writes with " + method + "\n";
    return text;
}
