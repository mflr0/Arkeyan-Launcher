#include "launcher/online/room_code.h"
#include "launcher/online/lobby_protocol.h"
#include "launcher/online/net_socket.h"
#include <array>
#include <cstring>
#include <span>
#include <vector>

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

        struct Shape
        {
            size_t letters, addressBytes, checkBits;
            bool port;
        };
        // Kind 0: IPv4 on the default port; 1: IPv4 and port; 2: IPv6 and port. Every shape fills its letters exactly.
        constexpr std::array<Shape, 3> Shapes{ { { 20, 4, 28, false }, { 24, 4, 32, true }, { 44, 16, 36, true } } };

        uint64_t Check(size_t kind, std::span<const uint8_t> bytes)
        {
            uint64_t hash = 14695981039346656037ull ^ uint64_t(kind + 1);
            hash *= 1099511628211ull;
            for (const uint8_t b : bytes)
            {
                hash ^= b;
                hash *= 1099511628211ull;
            }
            return hash;
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

    std::string EncodeRoomCode(std::string_view host, uint16_t port, uint64_t secret)
    {
        net::Startup();
        const std::string text(host);
        std::array<uint8_t, 16> address{};
        size_t kind = 0;
        if (inet_pton(AF_INET, text.c_str(), address.data()) == 1)
            kind = port == DefaultPort ? 0 : 1;
        else if (inet_pton(AF_INET6, text.c_str(), address.data()) == 1)
            kind = 2;
        else
            return "";
        const Shape& shape = Shapes[kind];
        std::vector<uint8_t> bytes(address.begin(), address.begin() + ptrdiff_t(shape.addressBytes));
        if (shape.port)
        {
            bytes.push_back(uint8_t(port >> 8));
            bytes.push_back(uint8_t(port));
        }
        for (int i = 4; i >= 0; --i)
            bytes.push_back(uint8_t(secret >> (8 * i)));
        const uint64_t check = Check(kind, bytes) >> (64 - shape.checkBits);

        // The bits, most significant first: the address, the port, the secret, then the check.
        std::vector<bool> bits;
        for (const uint8_t b : bytes)
            for (int i = 7; i >= 0; --i)
                bits.push_back(((b >> i) & 1) != 0);
        for (int i = int(shape.checkBits) - 1; i >= 0; --i)
            bits.push_back(((check >> i) & 1) != 0);

        std::string code;
        for (size_t letter = 0; letter < shape.letters; ++letter)
        {
            if (letter > 0 && letter % 4 == 0)
                code += '-';
            int value = 0;
            for (size_t i = 0; i < 5; ++i)
                value = value * 2 + (bits[letter * 5 + i] ? 1 : 0);
            code += Letters[size_t(value)];
        }
        return code;
    }

    bool DecodeRoomCode(std::string_view text, std::string& host, uint16_t& port, uint64_t& secret)
    {
        std::vector<int> values;
        for (const char c : text)
        {
            if (c == '-' || c == ' ' || c == '\t' || c == '\r' || c == '\n')
                continue;
            const int value = LetterValue(c);
            if (value < 0)
                return false;
            values.push_back(value);
        }
        size_t kind = 0;
        while (kind < Shapes.size() && Shapes[kind].letters != values.size())
            ++kind;
        if (kind == Shapes.size())
            return false;
        const Shape& shape = Shapes[kind];

        std::vector<bool> bits;
        for (const int value : values)
            for (int i = 4; i >= 0; --i)
                bits.push_back(((value >> i) & 1) != 0);
        size_t at = 0;
        const auto take = [&](size_t count) {
            uint64_t result = 0;
            for (size_t i = 0; i < count; ++i)
                result = result * 2 + (bits[at++] ? 1 : 0);
            return result;
        };
        std::vector<uint8_t> bytes;
        const size_t byteCount = shape.addressBytes + (shape.port ? 2 : 0) + 5;
        for (size_t i = 0; i < byteCount; ++i)
            bytes.push_back(uint8_t(take(8)));
        const uint64_t check = take(shape.checkBits);
        if (check != Check(kind, bytes) >> (64 - shape.checkBits))
            return false;

        net::Startup();
        char address[INET6_ADDRSTRLEN] = {};
        if (!inet_ntop(shape.addressBytes == 4 ? AF_INET : AF_INET6, bytes.data(), address, sizeof(address)))
            return false;
        secret = 0;
        for (size_t i = byteCount - 5; i < byteCount; ++i)
            secret = (secret << 8) | bytes[i];
        host = address;
        port = shape.port ? uint16_t((bytes[shape.addressBytes] << 8) | bytes[shape.addressBytes + 1]) : DefaultPort;
        return port != 0;
    }
}
