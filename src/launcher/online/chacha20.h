#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace online
{
    using CipherKey = std::array<uint8_t, 32>;
    using CipherNonce = std::array<uint8_t, 12>;
    std::array<uint8_t, 64> ChaCha20Block(const CipherKey& key, const CipherNonce& nonce, uint32_t counter);
    // Refuse counter exhaustion before changing any bytes.
    bool ChaCha20Xor(const CipherKey& key, const CipherNonce& nonce, uint32_t counter, std::span<uint8_t> bytes);
}
