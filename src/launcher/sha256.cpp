#include "launcher/sha256.h"
#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#if defined(__ARM_FEATURE_SHA2) && (defined(__aarch64__) || defined(_M_ARM64))
#include <arm_neon.h>
#define SHA256_ARM 1
#endif

namespace
{
    constexpr uint32_t K[64] = {
        0x428a2f98,
        0x71374491,
        0xb5c0fbcf,
        0xe9b5dba5,
        0x3956c25b,
        0x59f111f1,
        0x923f82a4,
        0xab1c5ed5,
        0xd807aa98,
        0x12835b01,
        0x243185be,
        0x550c7dc3,
        0x72be5d74,
        0x80deb1fe,
        0x9bdc06a7,
        0xc19bf174,
        0xe49b69c1,
        0xefbe4786,
        0x0fc19dc6,
        0x240ca1cc,
        0x2de92c6f,
        0x4a7484aa,
        0x5cb0a9dc,
        0x76f988da,
        0x983e5152,
        0xa831c66d,
        0xb00327c8,
        0xbf597fc7,
        0xc6e00bf3,
        0xd5a79147,
        0x06ca6351,
        0x14292967,
        0x27b70a85,
        0x2e1b2138,
        0x4d2c6dfc,
        0x53380d13,
        0x650a7354,
        0x766a0abb,
        0x81c2c92e,
        0x92722c85,
        0xa2bfe8a1,
        0xa81a664b,
        0xc24b8b70,
        0xc76c51a3,
        0xd192e819,
        0xd6990624,
        0xf40e3585,
        0x106aa070,
        0x19a4c116,
        0x1e376c08,
        0x2748774c,
        0x34b0bcb5,
        0x391c0cb3,
        0x4ed8aa4a,
        0x5b9cca4f,
        0x682e6ff3,
        0x748f82ee,
        0x78a5636f,
        0x84c87814,
        0x8cc70208,
        0x90befffa,
        0xa4506ceb,
        0xbef9a3f7,
        0xc67178f2,
    };
}

#ifndef SHA256_ARM
namespace
{
    uint32_t Rotate(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    uint32_t ReadBE32(const uint8_t* p)
    {
        return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }

    void PortableBlock(uint32_t* state, const uint8_t* block)
    {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ReadBE32(block + i * 4);
        for (int i = 16; i < 64; i++)
        {
            uint32_t s0 = Rotate(w[i - 15], 7) ^ Rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = Rotate(w[i - 2], 17) ^ Rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6],
                 h = state[7];
        for (int i = 0; i < 64; i++)
        {
            uint32_t t1 = h + (Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            uint32_t t2 = (Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }
}
#endif

#ifdef SHA256_ARM
namespace
{
    // The four rounds of each group and the message schedule in the CPU's SHA-256 instructions; the state as ABCD, EFGH.
    void ArmBlocks(uint32_t* state, const uint8_t* data, size_t count)
    {
        uint32x4_t abcd = vld1q_u32(state), efgh = vld1q_u32(state + 4);
        for (; count != 0; count--, data += 64)
        {
            const uint32x4_t abcdIn = abcd, efghIn = efgh;
            uint32x4_t w[4];
            for (int i = 0; i < 4; i++)
                w[i] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(data + 16 * i))); // big-endian words
            for (int i = 0; i < 16; i++)
            {
                const uint32x4_t wk = vaddq_u32(w[i & 3], vld1q_u32(&K[4 * i]));
                const uint32x4_t before = abcd;
                abcd = vsha256hq_u32(abcd, efgh, wk);
                efgh = vsha256h2q_u32(efgh, before, wk);
                if (i < 12) // the next 4 words of the schedule replace the 4 just used
                    w[i & 3] =
                        vsha256su1q_u32(vsha256su0q_u32(w[i & 3], w[(i + 1) & 3]), w[(i + 2) & 3], w[(i + 3) & 3]);
            }
            abcd = vaddq_u32(abcd, abcdIn);
            efgh = vaddq_u32(efgh, efghIn);
        }
        vst1q_u32(state, abcd);
        vst1q_u32(state + 4, efgh);
    }
}
#endif

bool Sha256::Accelerated()
{
#ifdef SHA256_ARM
    return true;
#else
    return false;
#endif
}

void Sha256::Blocks(const uint8_t* data, size_t count)
{
#ifdef SHA256_ARM
    ArmBlocks(m_hash, data, count);
#else
    for (; count != 0; count--, data += 64)
        PortableBlock(m_hash, data);
#endif
}

void Sha256::Update(std::span<const uint8_t> data)
{
    m_length += data.size();
    size_t at = 0;
    if (m_pendingSize != 0)
    {
        const size_t take = std::min(data.size(), sizeof(m_pending) - m_pendingSize);
        memcpy(m_pending + m_pendingSize, data.data(), take);
        m_pendingSize += take;
        at = take;
        if (m_pendingSize < sizeof(m_pending))
            return;
        Blocks(m_pending, 1);
        m_pendingSize = 0;
    }
    const size_t whole = (data.size() - at) / 64;
    Blocks(data.data() + at, whole);
    at += whole * 64;
    m_pendingSize = data.size() - at;
    if (m_pendingSize != 0)
        memcpy(m_pending, &data[at], m_pendingSize);
}

std::string Sha256::FinishHex()
{
    // Padding: 0x80, zeros, then the message length in bits (big-endian), filling one or two blocks.
    uint8_t tail[128] = {};
    memcpy(tail, m_pending, m_pendingSize);
    tail[m_pendingSize] = 0x80;
    const size_t tailSize = m_pendingSize < 56 ? 64 : 128;
    const uint64_t bits = m_length * 8;
    for (int i = 0; i < 8; i++)
        tail[tailSize - 1 - i] = uint8_t(bits >> (i * 8));
    Blocks(tail, tailSize / 64);

    std::string hex;
    for (uint32_t word : m_hash)
        hex += fmt::format("{:08x}", word);
    return hex;
}
