#include "launcher/online/chacha20.h"
#include <algorithm>
#include <bit>
#include <limits>

namespace online
{
    std::array<uint8_t, 64> ChaCha20Block(const CipherKey& key, const CipherNonce& nonce, uint32_t counter)
    {
        const auto word = [](const uint8_t* bytes) {
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
                value |= uint32_t(bytes[i]) << (8 * i);
            return value;
        };
        std::array<uint32_t, 16> initial{ 0x61707865, 0x3320646e, 0x79622d32, 0x6b206574 };
        for (size_t i = 0; i < 8; ++i)
            initial[4 + i] = word(key.data() + i * 4);
        initial[12] = counter;
        for (size_t i = 0; i < 3; ++i)
            initial[13 + i] = word(nonce.data() + i * 4);
        auto state = initial;
        const auto quarter = [&](size_t a, size_t b, size_t c, size_t d) {
            for (const int rotation : { 16, 12, 8, 7 })
            {
                state[a] += state[b];
                state[d] = std::rotl(state[d] ^ state[a], rotation);
                std::swap(a, c);
                std::swap(b, d);
            }
        };
        for (int round = 0; round < 10; ++round)
        {
            for (size_t column = 0; column < 4; ++column)
                quarter(column, column + 4, column + 8, column + 12);
            for (size_t diagonal = 0; diagonal < 4; ++diagonal)
                quarter(diagonal, 4 + (diagonal + 1) % 4, 8 + (diagonal + 2) % 4, 12 + (diagonal + 3) % 4);
        }
        std::array<uint8_t, 64> output{};
        for (size_t i = 0; i < state.size(); ++i)
        {
            const uint32_t value = state[i] + initial[i];
            for (int byte = 0; byte < 4; ++byte)
                output[i * 4 + byte] = uint8_t(value >> (8 * byte));
        }
        return output;
    }

    bool ChaCha20Xor(const CipherKey& key, const CipherNonce& nonce, uint32_t counter, std::span<uint8_t> bytes)
    {
        const uint64_t blocks = bytes.size() / 64 + (bytes.size() % 64 != 0);
        if (blocks > uint64_t(std::numeric_limits<uint32_t>::max()) - counter + 1)
            return false;
        size_t offset = 0;
        while (offset < bytes.size())
        {
            const auto stream = ChaCha20Block(key, nonce, counter++);
            const size_t count = std::min(stream.size(), bytes.size() - offset);
            for (size_t i = 0; i < count; ++i)
                bytes[offset + i] ^= stream[i];
            offset += count;
        }
        return true;
    }
}
