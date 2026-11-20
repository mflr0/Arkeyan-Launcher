#pragma once

// Opens the host's port on the home router, so the other player can reach it without a manual router setting
// (docs/runtime/host/netplay.md, step 4): UPnP IGD first (SSDP, the device description, SOAP), then NAT-PMP. No outside
// service is called: only the router on the local network. The message builders and parsers are pure, for the tests.
// The same file is the launcher's (src/shared/portmap.h), whose room maps TCP on the same port.
#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace netplay::portmap
{
    struct Url
    {
        std::string host;
        uint16_t port = 80;
        std::string path = "/";
    };
    // http://host[:port][/path] only (a router's description and control URLs).
    bool ParseUrl(std::string_view text, Url& url);
    // A control URL may be absolute or relative to the description's URL (or its URLBase).
    bool ResolveUrl(const Url& base, std::string_view reference, Url& resolved);

    // The SSDP search for gateways, sent to 239.255.255.250:1900.
    std::string SsdpSearch(std::string_view searchTarget);
    // An HTTP/1.1 200 answer to the search, with its LOCATION header.
    bool ParseSsdpResponse(std::string_view response, std::string& location);

    struct WanService
    {
        std::string type; // urn:schemas-upnp-org:service:WANIPConnection:1, ...:2 or WANPPPConnection:1
        Url control;
    };
    // The first WAN connection service of the device description at `descriptionUrl`.
    bool FindWanService(std::string_view xml, const Url& descriptionUrl, WanService& service);

    using SoapArgs = std::span<const std::pair<std::string_view, std::string>>;
    // The whole HTTP POST of one SOAP action.
    std::string SoapRequest(const Url& control, std::string_view serviceType, std::string_view action, SoapArgs args);
    struct SoapResult
    {
        int status = 0;     // HTTP status
        int errorCode = 0;  // UPnP fault code (718: the port is taken by another computer)
        std::string value;  // the requested field of a 200 answer
    };
    // `field` may be empty when the action returns nothing.
    bool ParseSoapResponse(std::string_view response, std::string_view field, SoapResult& result);
    // Status line, headers and body of an HTTP/1.x answer; the body is cut to Content-Length or de-chunked.
    bool SplitHttpResponse(std::string_view response, int& status, std::string& body);

    // NAT-PMP (RFC 6886), port 5351 of the gateway.
    std::array<uint8_t, 2> NatPmpAddressRequest();
    std::array<uint8_t, 12> NatPmpMapRequest(
        uint16_t internalPort, uint16_t externalPort, uint32_t lifetimeSeconds, bool tcp = false);
    bool ParseNatPmpAddress(std::span<const uint8_t> packet, uint16_t& result, uint32_t& address);
    bool ParseNatPmpMap(std::span<const uint8_t> packet, uint16_t& result, uint16_t& internalPort,
        uint16_t& externalPort, uint32_t& lifetimeSeconds, bool tcp = false);

    std::string Ipv4Text(uint32_t address); // host byte order
    bool ParseIpv4(std::string_view text, uint32_t& address);
    // False when the address is not one a remote player can reach (private, loopback, link-local, CGNAT, zero).
    bool IsPublicIpv4(uint32_t address);

    struct Options
    {
        uint16_t port = 47640; // the host's UDP port, the same inside and outside
        std::string description = "Arkeyan online";
        std::chrono::milliseconds timeout{ 2000 }; // per step
        uint32_t leaseSeconds = 7200;              // renewed by Map while the session lasts
        // Tests: where to send the SSDP search and NAT-PMP requests instead of the multicast group and the gateway.
        std::string ssdpTarget; // "a.b.c.d:port"
        std::string natPmpTarget; // "a.b.c.d:port"
        bool tryUpnp = true, tryNatPmp = true;
        bool alsoTcp = false; // the launchers' room uses TCP on the same port as the games' UDP
    };

    struct Mapping
    {
        bool ok = false;
        std::string method; // "UPnP" or "NAT-PMP"
        std::string externalIp; // as the router reports it (may be private: double NAT)
        uint16_t externalPort = 0;
        std::string error; // why nothing worked, for the log and the launcher
        // To remove it again.
        WanService service;
        std::string natPmpGateway;
    };

    Mapping Map(const Options& options);
    void Unmap(const Mapping& mapping, const Options& options);

    // The IPv4 default gateway, host byte order; false when the system does not say.
    bool DefaultGateway(uint32_t& address);
}
