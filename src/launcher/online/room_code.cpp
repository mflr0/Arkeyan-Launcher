#include "launcher/online/room_code.h"
#include "launcher/online/lobby_protocol.h"
#include <array>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace online
{
    namespace
    {
        constexpr std::string_view Letters = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        constexpr size_t CheckBits = 20, MaxLetters = 188;

        void Put(std::vector<bool>& bits, uint64_t value, size_t count)
        {
            for (size_t i = count; i > 0; --i)
                bits.push_back(((value >> (i - 1)) & 1) != 0);
        }

        uint64_t Check(const std::vector<bool>& bits, size_t count)
        {
            uint64_t hash = 14695981039346656037ull;
            for (size_t at = 0; at < count; at += 8)
            {
                uint8_t byte = 0;
                for (size_t i = 0; i < 8; ++i)
                    byte = uint8_t((byte << 1) | (at + i < count && bits[at + i] ? 1 : 0));
                hash = (hash ^ byte) * 1099511628211ull;
            }
            return hash >> (64 - CheckBits);
        }

        int LetterValue(char c)
        {
            if (c >= 'a' && c <= 'z')
                c = char(c - 'a' + 'A');
            if (c == 'O')
                c = '0';
            else if (c == 'I' || c == 'L')
                c = '1';
            const size_t at = Letters.find(c);
            return at == std::string_view::npos ? -1 : int(at);
        }
    }

    std::string EncodeRoomCode(std::span<const net::Endpoint> endpoints, uint64_t secret)
    {
        if (endpoints.empty() || endpoints.size() > 6)
            return "";
        net::Startup();
        std::vector<bool> bits;
        Put(bits, endpoints.size() - 1, 3);
        Put(bits, secret, 40);
        for (const auto& endpoint : endpoints)
        {
            std::array<uint8_t, 16> address{};
            const bool v6 = inet_pton(AF_INET, endpoint.ip.c_str(), address.data()) != 1;
            if (!endpoint.port || (v6 && inet_pton(AF_INET6, endpoint.ip.c_str(), address.data()) != 1))
                return "";
            const bool port = endpoint.port != (&endpoint == endpoints.data() ? DefaultPort : endpoints.front().port);
            Put(bits, v6, 1);
            Put(bits, port, 1);
            for (size_t i = 0; i < (v6 ? 16 : 4); ++i)
                Put(bits, address[i], 8);
            if (port)
                Put(bits, endpoint.port, 16);
        }
        // Zero padding makes every group complete without repeating a port or length for each endpoint.
        while (bits.size() % 20)
            bits.push_back(false);
        Put(bits, Check(bits, bits.size()), CheckBits);
        std::string code;
        for (size_t at = 0; at < bits.size(); at += 5)
        {
            if (at && at % 20 == 0)
                code += '-';
            size_t value = 0;
            for (size_t i = 0; i < 5; ++i)
                value = value * 2 + (bits[at + i] ? 1 : 0);
            code += Letters[value];
        }
        return code;
    }

    bool DecodeRoomCode(std::string_view text, std::vector<net::Endpoint>& endpoints, uint64_t& secret)
    {
        std::vector<bool> bits;
        for (const char c : text)
        {
            if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n')
                continue;
            const int value = LetterValue(c);
            if (value < 0 || bits.size() >= MaxLetters * 5)
                return false;
            Put(bits, uint64_t(value), 5);
        }
        if (bits.size() < 100 || bits.size() % 20)
            return false;
        size_t at = 0;
        bool valid = true;
        const auto take = [&](size_t count) {
            uint64_t value = 0;
            if (at + count > bits.size() - CheckBits)
            {
                valid = false;
                return value;
            }
            for (size_t i = 0; i < count; ++i)
                value = value * 2 + (bits[at++] ? 1 : 0);
            return value;
        };
        const size_t count = size_t(take(3)) + 1;
        if (count > 6)
            return false;
        const uint64_t decodedSecret = take(40);
        std::vector<net::Endpoint> decoded;
        net::Startup();
        for (size_t i = 0; i < count; ++i)
        {
            const bool v6 = take(1) != 0, hasPort = take(1) != 0;
            std::array<uint8_t, 16> address{};
            for (size_t b = 0; b < (v6 ? 16 : 4); ++b)
                address[b] = uint8_t(take(8));
            const uint16_t inherited = i ? decoded.front().port : DefaultPort;
            const uint16_t port = hasPort ? uint16_t(take(16)) : inherited;
            char ip[INET6_ADDRSTRLEN] = {};
            if (!valid || !port || (hasPort && port == inherited) ||
                !inet_ntop(v6 ? AF_INET6 : AF_INET, address.data(), ip, sizeof ip))
                return false;
            decoded.push_back({ ip, port });
        }
        const size_t padded = (at + 19) / 20 * 20;
        if (padded + CheckBits != bits.size())
            return false;
        while (at < padded)
            if (bits[at++])
                return false;
        uint64_t check = 0;
        for (; at < bits.size(); ++at)
            check = check * 2 + (bits[at] ? 1 : 0);
        if (check != Check(bits, padded))
            return false;
        endpoints = std::move(decoded);
        secret = decodedSecret;
        return true;
    }
}
