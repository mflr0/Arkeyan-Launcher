#include "portmap.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <net/route.h>
#include <sys/sysctl.h>
#endif
#endif

namespace netplay::portmap
{
    namespace
    {
        constexpr std::string_view SsdpGroup = "239.255.255.250:1900";
        constexpr uint16_t NatPmpPort = 5351;
        constexpr size_t MaxHttpBytes = 256 * 1024;
        // UPnP faults worth a second try or a clear message.
        constexpr int FaultConflict = 718;
        constexpr int FaultOnlyPermanentLeases = 725;

        bool EqualsNoCase(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
            });
        }

        bool StartsWithNoCase(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && EqualsNoCase(text.substr(0, prefix.size()), prefix);
        }

        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        template <typename T>
        bool ParseNumber(std::string_view text, T& value, int radix = 10)
        {
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, radix);
            return error == std::errc{} && end == text.data() + text.size() && !text.empty();
        }

        std::string XmlUnescape(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] != '&')
                {
                    out += text[i];
                    continue;
                }
                const size_t semi = text.find(';', i);
                const auto entity =
                    semi == std::string_view::npos ? std::string_view{} : text.substr(i + 1, semi - i - 1);
                char c = 0;
                if (entity == "amp")
                    c = '&';
                else if (entity == "lt")
                    c = '<';
                else if (entity == "gt")
                    c = '>';
                else if (entity == "quot")
                    c = '"';
                else if (entity == "apos")
                    c = '\'';
                if (!c)
                {
                    out += '&';
                    continue;
                }
                out += c;
                i = semi;
            }
            return out;
        }

        std::string XmlEscape(std::string_view text)
        {
            std::string out;
            for (const char c : text)
            {
                switch (c)
                {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                default: out += c;
                }
            }
            return out;
        }

        // The text of the next element whose local name (the part after any namespace prefix) is `name`, at or after
        // `from`; `next` is just past its end tag. Routers' XML is simple: no CDATA, no nesting of the same name.
        bool FindElement(std::string_view xml, std::string_view name, size_t from, std::string_view& text, size_t& next)
        {
            for (size_t open = xml.find('<', from); open != std::string_view::npos; open = xml.find('<', open + 1))
            {
                size_t nameEnd = open + 1;
                while (nameEnd < xml.size() && !std::isspace(static_cast<unsigned char>(xml[nameEnd])) &&
                       xml[nameEnd] != '>' && xml[nameEnd] != '/')
                    ++nameEnd;
                auto tag = xml.substr(open + 1, nameEnd - open - 1);
                if (tag.empty() || tag[0] == '/' || tag[0] == '?' || tag[0] == '!')
                    continue;
                const size_t colon = tag.find(':');
                const auto local = colon == std::string_view::npos ? tag : tag.substr(colon + 1);
                if (local != name)
                    continue;
                const size_t close = xml.find('>', nameEnd);
                if (close == std::string_view::npos)
                    return false;
                if (xml[close - 1] == '/')
                {
                    text = {};
                    next = close + 1;
                    return true;
                }
                const std::string end = "</" + std::string(tag) + ">";
                const size_t endAt = xml.find(end, close + 1);
                if (endAt == std::string_view::npos)
                    return false;
                text = xml.substr(close + 1, endAt - close - 1);
                next = endAt + end.size();
                return true;
            }
            return false;
        }

        bool ParseHostPort(std::string_view text, uint32_t& address, uint16_t& port)
        {
            const size_t colon = text.rfind(':');
            return colon != std::string_view::npos && ParseIpv4(text.substr(0, colon), address) &&
                   ParseNumber(text.substr(colon + 1), port) && port != 0;
        }

        // Sockets: blocking calls bounded by poll, IPv4 only (home routers answer UPnP and NAT-PMP on IPv4).
#if defined(_WIN32)
        using Socket = SOCKET;
        const Socket NoSocket = INVALID_SOCKET;
        void CloseSocket(Socket s) { closesocket(s); }
        int PollOne(Socket s, short events, int timeoutMs)
        {
            WSAPOLLFD fd{ s, events, 0 };
            return WSAPoll(&fd, 1, timeoutMs);
        }
        bool InProgress() { return WSAGetLastError() == WSAEWOULDBLOCK; }
        void SetNonBlocking(Socket s, bool on)
        {
            u_long mode = on ? 1 : 0;
            ioctlsocket(s, FIONBIO, &mode);
        }
#else
        using Socket = int;
        const Socket NoSocket = -1;
        void CloseSocket(Socket s) { close(s); }
        int PollOne(Socket s, short events, int timeoutMs)
        {
            pollfd fd{ s, events, 0 };
            return poll(&fd, 1, timeoutMs);
        }
        bool InProgress() { return errno == EINPROGRESS; }
        void SetNonBlocking(Socket s, bool on)
        {
            const int flags = fcntl(s, F_GETFL, 0);
            fcntl(s, F_SETFL, on ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
        }
#endif

        bool Startup()
        {
#if defined(_WIN32)
            static std::once_flag once;
            static bool ok = false;
            std::call_once(once, [] {
                WSADATA data;
                ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
            });
            return ok;
#else
            return true;
#endif
        }

        sockaddr_in Address(uint32_t ipv4, uint16_t port)
        {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(ipv4);
            address.sin_port = htons(port);
            return address;
        }

        int Remaining(std::chrono::steady_clock::time_point deadline)
        {
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            return int(std::max<int64_t>(0, left.count()));
        }

        // Sends `request` to `to`, waits for answers until `accept` takes one or the time runs out. `fromTarget`:
        // only answers from `to` itself count (NAT-PMP; an SSDP answer comes from the device, not the group).
        template <typename Accept>
        bool UdpExchange(const sockaddr_in& to, std::span<const uint8_t> request, std::chrono::milliseconds timeout,
            bool fromTarget, Accept&& accept)
        {
            if (!Startup())
                return false;
            const Socket s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (s == NoSocket)
                return false;
            const unsigned char ttl = 2;
            setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl));
            bool done = false;
            // Two sends: a lost datagram on a busy network should not cost the whole timeout.
            for (int attempt = 0; attempt < 2 && !done; ++attempt)
            {
                if (sendto(s, reinterpret_cast<const char*>(request.data()), int(request.size()), 0,
                        reinterpret_cast<const sockaddr*>(&to), sizeof(to)) < 0)
                    break;
                const auto deadline = std::chrono::steady_clock::now() + timeout / 2;
                while (!done)
                {
                    const int wait = Remaining(deadline);
                    if (wait <= 0 || PollOne(s, POLLIN, wait) <= 0)
                        break;
                    uint8_t buffer[2048];
                    sockaddr_in from{};
                    socklen_t fromSize = sizeof(from);
                    const auto got = recvfrom(s, reinterpret_cast<char*>(buffer), int(sizeof(buffer)), 0,
                        reinterpret_cast<sockaddr*>(&from), &fromSize);
                    if (got <= 0)
                        continue;
                    if (fromTarget && (from.sin_addr.s_addr != to.sin_addr.s_addr || from.sin_port != to.sin_port))
                        continue;
                    done = accept(std::span<const uint8_t>(buffer, size_t(got)), from);
                }
            }
            CloseSocket(s);
            return done;
        }

        // The headers say where the body ends (a length or chunks), so a complete parse means a complete answer.
        bool HasFramedBody(std::string_view response)
        {
            const size_t headerEnd = response.find("\r\n\r\n");
            if (headerEnd == std::string_view::npos)
                return false;
            std::string headers(response.substr(0, headerEnd));
            std::transform(
                headers.begin(), headers.end(), headers.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return headers.find("\ncontent-length:") != std::string::npos ||
                   headers.find("\ntransfer-encoding:") != std::string::npos;
        }

        // One HTTP/1.1 exchange with `Connection: close`; `local` gets this computer's address on that route.
        bool HttpExchange(const Url& url, std::string_view request, std::chrono::milliseconds timeout,
            std::string& response, uint32_t& local)
        {
            uint32_t ipv4 = 0;
            if (!Startup() || !ParseIpv4(url.host, ipv4))
                return false;
            const Socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (s == NoSocket)
                return false;
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            const sockaddr_in to = Address(ipv4, url.port);
            SetNonBlocking(s, true);
            bool ok = connect(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) == 0 ||
                      (InProgress() && PollOne(s, POLLOUT, Remaining(deadline)) > 0);
            if (ok)
            {
                int error = 0;
                socklen_t size = sizeof(error);
                ok = getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) == 0 && error == 0;
            }
            if (ok)
            {
                sockaddr_in self{};
                socklen_t size = sizeof(self);
                ok = getsockname(s, reinterpret_cast<sockaddr*>(&self), &size) == 0;
                local = ntohl(self.sin_addr.s_addr);
            }
            for (size_t sent = 0; ok && sent < request.size();)
            {
                if (PollOne(s, POLLOUT, Remaining(deadline)) <= 0)
                {
                    ok = false;
                    break;
                }
                const auto n = send(s, request.data() + sent, int(request.size() - sent), 0);
                if (n <= 0)
                    ok = false;
                else
                    sent += size_t(n);
            }
            response.clear();
            while (ok && response.size() < MaxHttpBytes)
            {
                if (PollOne(s, POLLIN, Remaining(deadline)) <= 0)
                    break;
                char buffer[4096];
                const auto n = recv(s, buffer, int(sizeof(buffer)), 0);
                if (n <= 0)
                    break;
                response.append(buffer, size_t(n));
                // A router that keeps the connection open despite `Connection: close`: stop once the body is whole.
                int status = 0;
                std::string body;
                if (HasFramedBody(response) && SplitHttpResponse(response, status, body))
                    break;
            }
            CloseSocket(s);
            return ok && !response.empty();
        }

        std::string HttpGet(const Url& url)
        {
            return "GET " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" + std::to_string(url.port) +
                   "\r\nConnection: close\r\nUser-Agent: Arkeyan/1.0 UPnP/1.1\r\n\r\n";
        }

        bool Soap(const WanService& service, std::string_view action, SoapArgs args, std::string_view field,
            std::chrono::milliseconds timeout, SoapResult& result)
        {
            std::string response;
            uint32_t local = 0;
            return HttpExchange(service.control, SoapRequest(service.control, service.type, action, args), timeout,
                       response, local) &&
                   ParseSoapResponse(response, field, result);
        }

        bool FindGateway(const Options& options, WanService& service, uint32_t& local, std::string& error)
        {
            uint32_t groupIp = 0;
            uint16_t groupPort = 0;
            if (!ParseHostPort(options.ssdpTarget.empty() ? SsdpGroup : options.ssdpTarget, groupIp, groupPort))
            {
                error = "bad SSDP target";
                return false;
            }
            std::string location;
            // Every IGD answers its device type; WANIPConnection alone would miss the PPP-only ones.
            const std::string first = SsdpSearch("urn:schemas-upnp-org:device:InternetGatewayDevice:1");
            const bool answered = UdpExchange(Address(groupIp, groupPort),
                std::span(reinterpret_cast<const uint8_t*>(first.data()), first.size()), options.timeout, false,
                [&](std::span<const uint8_t> packet, const sockaddr_in&) {
                    return ParseSsdpResponse(
                        std::string_view(reinterpret_cast<const char*>(packet.data()), packet.size()), location);
                });
            if (!answered)
            {
                error = "no UPnP router answered";
                return false;
            }
            Url description;
            if (!ParseUrl(location, description))
            {
                error = "the router's UPnP address is not usable: " + location;
                return false;
            }
            std::string response, body;
            int status = 0;
            if (!HttpExchange(description, HttpGet(description), options.timeout, response, local) ||
                !SplitHttpResponse(response, status, body) || status != 200)
            {
                error = "the router's UPnP description did not load";
                return false;
            }
            if (!FindWanService(body, description, service))
            {
                error = "the router offers no UPnP port mapping";
                return false;
            }
            return true;
        }

        bool MapUpnp(const Options& options, Mapping& mapping, std::string& error)
        {
            WanService service;
            uint32_t local = 0;
            if (!FindGateway(options, service, local, error))
                return false;
            const std::string port = std::to_string(options.port);
            std::vector<const char*> protocols = { "UDP" };
            if (options.alsoTcp)
                protocols.push_back("TCP");
            for (const char* protocol : protocols)
                for (uint32_t lease : { options.leaseSeconds, 0u })
                {
                    const std::pair<std::string_view, std::string> args[] = {
                        { "NewRemoteHost", "" },
                        { "NewExternalPort", port },
                        { "NewProtocol", protocol },
                        { "NewInternalPort", port },
                        { "NewInternalClient", Ipv4Text(local) },
                        { "NewEnabled", "1" },
                        { "NewPortMappingDescription", options.description },
                        { "NewLeaseDuration", std::to_string(lease) },
                    };
                    SoapResult result;
                    if (!Soap(service, "AddPortMapping", args, "", options.timeout, result))
                    {
                        error = "the router did not answer the UPnP port request";
                        return false;
                    }
                    if (result.status == 200)
                        break;
                    if (result.errorCode == FaultOnlyPermanentLeases && lease != 0)
                        continue;
                    error = result.errorCode == FaultConflict ? "the router gives port " + port + " to another computer"
                                                              : "the router refused the UPnP port request (error " +
                                                                    std::to_string(result.errorCode) + ", HTTP " +
                                                                    std::to_string(result.status) + ")";
                    return false;
                }
            SoapResult address;
            if (Soap(service, "GetExternalIPAddress", {}, "NewExternalIPAddress", options.timeout, address) &&
                address.status == 200)
                mapping.externalIp = address.value;
            mapping.ok = true;
            mapping.method = "UPnP";
            mapping.externalPort = options.port;
            mapping.service = service;
            return true;
        }

        bool NatPmpTarget(const Options& options, sockaddr_in& to)
        {
            uint32_t ip = 0;
            uint16_t port = NatPmpPort;
            if (!options.natPmpTarget.empty())
            {
                if (!ParseHostPort(options.natPmpTarget, ip, port))
                    return false;
            }
            else if (!DefaultGateway(ip))
                return false;
            to = Address(ip, port);
            return true;
        }

        bool MapNatPmp(
            const Options& options, uint16_t externalPort, uint32_t lifetime, Mapping& mapping, std::string& error)
        {
            sockaddr_in to{};
            if (!NatPmpTarget(options, to))
            {
                error = "no default gateway";
                return false;
            }
            const auto addressRequest = NatPmpAddressRequest();
            uint32_t external = 0;
            uint16_t result = 0;
            const bool answered = UdpExchange(
                to, addressRequest, options.timeout, true, [&](std::span<const uint8_t> packet, const sockaddr_in&) {
                    return ParseNatPmpAddress(packet, result, external);
                });
            if (!answered)
            {
                error = "no NAT-PMP router answered";
                return false;
            }
            uint16_t mapped = 0;
            for (const bool tcp : { false, true })
            {
                if (tcp && !options.alsoTcp)
                    break;
                const auto mapRequest = NatPmpMapRequest(options.port, externalPort, lifetime, tcp);
                uint16_t internal = 0, granted = 0;
                uint32_t seconds = 0;
                if (!UdpExchange(to, mapRequest, options.timeout, true,
                        [&](std::span<const uint8_t> packet, const sockaddr_in&) {
                            return ParseNatPmpMap(packet, result, internal, granted, seconds, tcp);
                        }))
                {
                    error = "the router did not answer the NAT-PMP port request";
                    return false;
                }
                if (result != 0 || internal != options.port || (lifetime != 0 && granted == 0))
                {
                    error = "the router refused the NAT-PMP port request (result " + std::to_string(result) + ")";
                    return false;
                }
                // The players share one port number: the UDP one; a router that gives TCP another one is refused.
                if (!tcp)
                    mapped = granted;
                else if (lifetime != 0 && granted != mapped)
                {
                    error = "the router gave the room another port than the game";
                    return false;
                }
            }
            mapping.ok = true;
            mapping.method = "NAT-PMP";
            mapping.externalIp = Ipv4Text(external);
            mapping.externalPort = mapped;
            mapping.natPmpGateway = Ipv4Text(ntohl(to.sin_addr.s_addr));
            return true;
        }
    }

    bool ParseUrl(std::string_view text, Url& url)
    {
        text = Trim(text);
        if (!StartsWithNoCase(text, "http://"))
            return false;
        text.remove_prefix(7);
        const size_t slash = text.find('/');
        const auto authority = text.substr(0, slash);
        url.path = slash == std::string_view::npos ? "/" : std::string(text.substr(slash));
        const size_t colon = authority.find(':');
        url.host = std::string(authority.substr(0, colon));
        url.port = 80;
        if (colon != std::string_view::npos && (!ParseNumber(authority.substr(colon + 1), url.port) || url.port == 0))
            return false;
        return !url.host.empty() && url.host.find_first_of("[]@ ") == std::string::npos;
    }

    bool ResolveUrl(const Url& base, std::string_view reference, Url& resolved)
    {
        reference = Trim(reference);
        if (StartsWithNoCase(reference, "http://"))
            return ParseUrl(reference, resolved);
        if (reference.empty() || reference.find("://") != std::string_view::npos)
            return false;
        resolved = base;
        if (reference.front() == '/')
            resolved.path = std::string(reference);
        else
            resolved.path = base.path.substr(0, base.path.rfind('/') + 1) + std::string(reference);
        return true;
    }

    std::string SsdpSearch(std::string_view searchTarget)
    {
        return "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: " +
               std::string(searchTarget) + "\r\n\r\n";
    }

    bool ParseSsdpResponse(std::string_view response, std::string& location)
    {
        if (!StartsWithNoCase(response, "HTTP/1.1 200") && !StartsWithNoCase(response, "HTTP/1.0 200"))
            return false;
        size_t at = response.find("\r\n");
        while (at != std::string_view::npos)
        {
            at += 2;
            const size_t end = response.find("\r\n", at);
            const auto line = response.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
            if (line.empty())
                break;
            const size_t colon = line.find(':');
            if (colon != std::string_view::npos && EqualsNoCase(Trim(line.substr(0, colon)), "location"))
            {
                location = std::string(Trim(line.substr(colon + 1)));
                return !location.empty();
            }
            at = end;
        }
        return false;
    }

    bool FindWanService(std::string_view xml, const Url& descriptionUrl, WanService& service)
    {
        Url base = descriptionUrl;
        std::string_view text;
        size_t next = 0;
        if (FindElement(xml, "URLBase", 0, text, next) && !Trim(text).empty())
        {
            Url parsed;
            if (ParseUrl(XmlUnescape(text), parsed))
                base = parsed;
        }
        bool found = false;
        for (size_t at = 0; FindElement(xml, "service", at, text, next); at = next)
        {
            std::string_view type, control;
            size_t ignored = 0;
            if (!FindElement(text, "serviceType", 0, type, ignored) ||
                !FindElement(text, "controlURL", 0, control, ignored))
                continue;
            const std::string typeText = XmlUnescape(Trim(type));
            const bool ip = typeText.starts_with("urn:schemas-upnp-org:service:WANIPConnection:");
            const bool ppp = typeText.starts_with("urn:schemas-upnp-org:service:WANPPPConnection:");
            if (!ip && !ppp)
                continue;
            WanService candidate{ typeText, {} };
            if (!ResolveUrl(base, XmlUnescape(control), candidate.control))
                continue;
            // A router with both keeps its live connection as WANIPConnection; PPP only when it has nothing else.
            if (ip || !found)
            {
                service = candidate;
                found = true;
            }
            if (ip)
                return true;
        }
        return found;
    }

    std::string SoapRequest(const Url& control, std::string_view serviceType, std::string_view action, SoapArgs args)
    {
        std::string body =
            "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
            "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:";
        body += action;
        body += " xmlns:u=\"";
        body += serviceType;
        body += "\">";
        for (const auto& [name, value] : args)
        {
            body += '<';
            body += name;
            body += '>';
            body += XmlEscape(value);
            body += "</";
            body += name;
            body += '>';
        }
        body += "</u:";
        body += action;
        body += "></s:Body></s:Envelope>\r\n";
        return "POST " + control.path + " HTTP/1.1\r\nHost: " + control.host + ":" + std::to_string(control.port) +
               "\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" + std::string(serviceType) + "#" +
               std::string(action) + "\"\r\nContent-Length: " + std::to_string(body.size()) +
               "\r\nConnection: close\r\n\r\n" + body;
    }

    bool SplitHttpResponse(std::string_view response, int& status, std::string& body)
    {
        if (!StartsWithNoCase(response, "HTTP/1."))
            return false;
        const size_t space = response.find(' ');
        if (space == std::string_view::npos || space + 4 > response.size() ||
            !ParseNumber(response.substr(space + 1, 3), status))
            return false;
        const size_t headerEnd = response.find("\r\n\r\n");
        if (headerEnd == std::string_view::npos)
            return false;
        const auto headers = response.substr(0, headerEnd);
        auto rest = response.substr(headerEnd + 4);
        bool chunked = false;
        size_t length = std::string_view::npos;
        for (size_t at = headers.find("\r\n"); at != std::string_view::npos;)
        {
            at += 2;
            const size_t end = headers.find("\r\n", at);
            const auto line = headers.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
            const size_t colon = line.find(':');
            if (colon != std::string_view::npos)
            {
                const auto name = Trim(line.substr(0, colon));
                const auto value = Trim(line.substr(colon + 1));
                if (EqualsNoCase(name, "transfer-encoding") && EqualsNoCase(value, "chunked"))
                    chunked = true;
                else if (EqualsNoCase(name, "content-length") && !ParseNumber(value, length))
                    return false;
            }
            at = end;
        }
        body.clear();
        if (chunked)
        {
            while (true)
            {
                const size_t eol = rest.find("\r\n");
                if (eol == std::string_view::npos)
                    return false;
                auto sizeText = rest.substr(0, eol);
                sizeText = Trim(sizeText.substr(0, sizeText.find(';')));
                size_t size = 0;
                if (!ParseNumber(sizeText, size, 16) || size > MaxHttpBytes || eol + 2 + size > rest.size())
                    return false;
                if (size == 0)
                    return true;
                body.append(rest.substr(eol + 2, size));
                rest.remove_prefix(std::min(rest.size(), eol + 2 + size + 2));
            }
        }
        if (length != std::string_view::npos)
        {
            if (length > rest.size())
                return false;
            rest = rest.substr(0, length);
        }
        body = std::string(rest);
        return true;
    }

    bool ParseSoapResponse(std::string_view response, std::string_view field, SoapResult& result)
    {
        std::string body;
        if (!SplitHttpResponse(response, result.status, body))
            return false;
        std::string_view text;
        size_t next = 0;
        if (result.status != 200)
        {
            if (FindElement(body, "errorCode", 0, text, next))
                ParseNumber(Trim(text), result.errorCode);
            return true;
        }
        if (field.empty())
            return true;
        if (!FindElement(body, field, 0, text, next))
            return false;
        result.value = XmlUnescape(Trim(text));
        return true;
    }

    std::array<uint8_t, 2> NatPmpAddressRequest() { return { 0, 0 }; }

    std::array<uint8_t, 12> NatPmpMapRequest(
        uint16_t internalPort, uint16_t externalPort, uint32_t lifetimeSeconds, bool tcp)
    {
        return { 0, uint8_t(tcp ? 2 : 1), 0, 0, uint8_t(internalPort >> 8), uint8_t(internalPort),
            uint8_t(externalPort >> 8), uint8_t(externalPort), uint8_t(lifetimeSeconds >> 24),
            uint8_t(lifetimeSeconds >> 16), uint8_t(lifetimeSeconds >> 8), uint8_t(lifetimeSeconds) };
    }

    namespace
    {
        uint16_t Be16(std::span<const uint8_t> p, size_t at) { return uint16_t(p[at] << 8 | p[at + 1]); }
        uint32_t Be32(std::span<const uint8_t> p, size_t at)
        {
            return uint32_t(p[at]) << 24 | uint32_t(p[at + 1]) << 16 | uint32_t(p[at + 2]) << 8 | p[at + 3];
        }
    }

    bool ParseNatPmpAddress(std::span<const uint8_t> packet, uint16_t& result, uint32_t& address)
    {
        if (packet.size() < 8 || packet[0] != 0 || packet[1] != 128)
            return false;
        result = Be16(packet, 2);
        if (result != 0)
            return true;
        if (packet.size() < 12)
            return false;
        address = Be32(packet, 8);
        return true;
    }

    bool ParseNatPmpMap(std::span<const uint8_t> packet, uint16_t& result, uint16_t& internalPort,
        uint16_t& externalPort, uint32_t& lifetimeSeconds, bool tcp)
    {
        if (packet.size() < 8 || packet[0] != 0 || packet[1] != (tcp ? 130 : 129))
            return false;
        result = Be16(packet, 2);
        if (packet.size() < 16)
            return result != 0;
        internalPort = Be16(packet, 8);
        externalPort = Be16(packet, 10);
        lifetimeSeconds = Be32(packet, 12);
        return true;
    }

    std::string Ipv4Text(uint32_t address)
    {
        char text[16];
        snprintf(text, sizeof(text), "%u.%u.%u.%u", address >> 24, (address >> 16) & 0xFF, (address >> 8) & 0xFF,
            address & 0xFF);
        return text;
    }

    bool ParseIpv4(std::string_view text, uint32_t& address)
    {
        uint32_t value = 0;
        for (int part = 0; part < 4; ++part)
        {
            const size_t dot = part < 3 ? text.find('.') : text.size();
            if (dot == std::string_view::npos || dot == 0 || dot > 3)
                return false;
            unsigned octet = 0;
            if (!ParseNumber(text.substr(0, dot), octet) || octet > 255)
                return false;
            value = value << 8 | octet;
            text.remove_prefix(part < 3 ? dot + 1 : dot);
        }
        address = value;
        return text.empty();
    }

    bool IsPublicIpv4(uint32_t a)
    {
        const auto in = [a](uint32_t net, int bits) { return (a >> (32 - bits)) == (net >> (32 - bits)); };
        return !(in(0x00000000, 8) || in(0x0A000000, 8) || in(0x64400000, 10) || in(0x7F000000, 8) ||
                 in(0xA9FE0000, 16) || in(0xAC100000, 12) || in(0xC0A80000, 16) || in(0xE0000000, 3));
    }

    Mapping Map(const Options& options)
    {
        Mapping mapping;
        std::string upnpError = "not tried", natPmpError = "not tried";
        if (options.tryUpnp && MapUpnp(options, mapping, upnpError))
            return mapping;
        if (options.tryNatPmp && MapNatPmp(options, options.port, options.leaseSeconds, mapping, natPmpError))
            return mapping;
        mapping = {};
        mapping.error = "UPnP: " + upnpError + "; NAT-PMP: " + natPmpError;
        return mapping;
    }

    void Unmap(const Mapping& mapping, const Options& options)
    {
        if (!mapping.ok)
            return;
        if (mapping.method == "UPnP")
        {
            for (const char* protocol : { "UDP", "TCP" })
            {
                if (std::string_view(protocol) == "TCP" && !options.alsoTcp)
                    break;
                const std::pair<std::string_view, std::string> args[] = {
                    { "NewRemoteHost", "" },
                    { "NewExternalPort", std::to_string(mapping.externalPort) },
                    { "NewProtocol", protocol },
                };
                SoapResult result;
                Soap(mapping.service, "DeletePortMapping", args, "", options.timeout, result);
            }
        }
        else
        {
            Mapping ignored;
            std::string error;
            Options removal = options;
            if (removal.natPmpTarget.empty())
                removal.natPmpTarget = mapping.natPmpGateway + ":" + std::to_string(NatPmpPort);
            // RFC 6886: lifetime 0 and external port 0 remove the mapping.
            MapNatPmp(removal, 0, 0, ignored, error);
        }
    }

    bool DefaultGateway(uint32_t& address)
    {
#if defined(__linux__)
        FILE* routes = fopen("/proc/net/route", "r");
        if (!routes)
            return false;
        char line[256];
        bool found = false;
        while (!found && fgets(line, sizeof(line), routes))
        {
            char name[64];
            unsigned destination = 0, gateway = 0, flags = 0;
            // The kernel prints both addresses in network byte order, as hex of the in-memory word.
            if (sscanf(line, "%63s %x %x %x", name, &destination, &gateway, &flags) == 4 && destination == 0 &&
                (flags & 0x2) && gateway != 0)
            {
                address = ntohl(gateway);
                found = true;
            }
        }
        fclose(routes);
        return found;
#elif defined(__APPLE__)
        int request[] = { CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS, RTF_GATEWAY };
        size_t size = 0;
        if (sysctl(request, 6, nullptr, &size, nullptr, 0) != 0 || size == 0)
            return false;
        std::vector<char> table(size);
        if (sysctl(request, 6, table.data(), &size, nullptr, 0) != 0)
            return false;
        // The kernel aligns each address of a routing message to 4 bytes.
        const auto advance = [](const sockaddr* sa) {
            return sa->sa_len ? size_t(1 + ((sa->sa_len - 1) | (sizeof(uint32_t) - 1))) : sizeof(uint32_t);
        };
        for (size_t at = 0; at + sizeof(rt_msghdr) <= size;)
        {
            const auto* message = reinterpret_cast<const rt_msghdr*>(table.data() + at);
            if (message->rtm_msglen == 0)
                break;
            const sockaddr* parts[RTAX_MAX] = {};
            size_t offset = at + sizeof(rt_msghdr);
            for (int i = 0; i < RTAX_MAX && offset < at + message->rtm_msglen; ++i)
            {
                if (!(message->rtm_addrs & (1 << i)))
                    continue;
                parts[i] = reinterpret_cast<const sockaddr*>(table.data() + offset);
                offset += advance(parts[i]);
            }
            const auto* destination = reinterpret_cast<const sockaddr_in*>(parts[RTAX_DST]);
            const auto* gateway = reinterpret_cast<const sockaddr_in*>(parts[RTAX_GATEWAY]);
            if (destination && gateway && destination->sin_family == AF_INET && gateway->sin_family == AF_INET &&
                destination->sin_addr.s_addr == 0 && gateway->sin_addr.s_addr != 0)
            {
                address = ntohl(gateway->sin_addr.s_addr);
                return true;
            }
            at += message->rtm_msglen;
        }
        return false;
#elif defined(_WIN32) && (!defined(WINAPI_FAMILY) || WINAPI_FAMILY == WINAPI_FAMILY_DESKTOP_APP)
        // A routing-table lookup for any outside address: nothing is sent.
        MIB_IPFORWARDROW row{};
        if (GetBestRoute(htonl(0x01010101), 0, &row) != NO_ERROR || row.dwForwardNextHop == 0)
            return false;
        address = ntohl(row.dwForwardNextHop);
        return true;
#else
        (void)address;
        return false;
#endif
    }
}
