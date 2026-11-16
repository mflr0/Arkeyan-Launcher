#pragma once

// SHA-256 fed in pieces: a disc's files are hashed in blocks, never read whole (disc_check.h). Sha256Hex (launcher.h) is
// the one-shot form. On an ARM CPU built with its SHA-256 instructions (Apple silicon) the blocks go through them, about
// ten times faster than the portable code, which every other CPU uses.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

class Sha256
{
public:
    void Update(std::span<const uint8_t> data);
    void Update(std::string_view text) { Update({ reinterpret_cast<const uint8_t*>(text.data()), text.size() }); }
    // The digest as lowercase hex. The object is spent: start a new one for the next.
    std::string FinishHex();

    static bool Accelerated(); // the CPU's SHA-256 instructions are used

private:
    void Blocks(const uint8_t* data, size_t count); // `count` blocks of 64 bytes

    uint32_t m_hash[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
        0x5be0cd19 };
    uint8_t m_pending[64] = {};
    size_t m_pendingSize = 0;
    uint64_t m_length = 0; // bytes fed so far
};
