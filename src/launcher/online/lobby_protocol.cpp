#include "launcher/online/lobby_protocol.h"
#include "launcher/sha256.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace online
{
    namespace
    {
        constexpr size_t MaxText = 256;
        constexpr size_t HeaderBytes = 1 + 4; // type, sequence

        std::array<uint8_t, 32> Digest(std::span<const uint8_t> first, std::span<const uint8_t> second = {})
        {
            Sha256 sha;
            sha.Update(first);
            sha.Update(second);
            const std::string hex = sha.FinishHex();
            std::array<uint8_t, 32> out{};
            for (size_t i = 0; i < out.size(); ++i)
            {
                const auto nibble = [](char c) { return uint8_t(c <= '9' ? c - '0' : c - 'a' + 10); };
                out[i] = uint8_t(nibble(hex[2 * i]) << 4 | nibble(hex[2 * i + 1]));
            }
            return out;
        }

        std::span<const uint8_t> AsBytes(std::string_view text)
        {
            return { reinterpret_cast<const uint8_t*>(text.data()), text.size() };
        }
    }

    void Writer::U16(uint16_t value)
    {
        U8(uint8_t(value));
        U8(uint8_t(value >> 8));
    }
    void Writer::U32(uint32_t value)
    {
        U16(uint16_t(value));
        U16(uint16_t(value >> 16));
    }
    void Writer::U64(uint64_t value)
    {
        U32(uint32_t(value));
        U32(uint32_t(value >> 32));
    }
    void Writer::Text(std::string_view text)
    {
        U16(uint16_t(text.size()));
        Raw(AsBytes(text));
    }
    void Writer::Blob(std::span<const uint8_t> bytes)
    {
        U32(uint32_t(bytes.size()));
        Raw(bytes);
    }
    void Writer::Raw(std::span<const uint8_t> bytes) { m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end()); }

    bool Reader::Need(size_t count)
    {
        if (!m_ok || m_bytes.size() - m_at < count)
            m_ok = false;
        return m_ok;
    }
    uint8_t Reader::U8() { return Need(1) ? m_bytes[m_at++] : 0; }
    uint16_t Reader::U16()
    {
        const uint16_t low = U8();
        return uint16_t(low | U8() << 8);
    }
    uint32_t Reader::U32()
    {
        const uint32_t low = U16();
        return low | uint32_t(U16()) << 16;
    }
    uint64_t Reader::U64()
    {
        const uint64_t low = U32();
        return low | uint64_t(U32()) << 32;
    }
    std::string Reader::Text(size_t max)
    {
        const size_t size = U16();
        if (size > max || !Need(size))
        {
            m_ok = false;
            return {};
        }
        std::string text(reinterpret_cast<const char*>(m_bytes.data() + m_at), size);
        m_at += size;
        return text;
    }
    std::vector<uint8_t> Reader::Blob(size_t max)
    {
        const size_t size = U32();
        if (size > max || !Need(size))
        {
            m_ok = false;
            return {};
        }
        std::vector<uint8_t> bytes(m_bytes.begin() + ptrdiff_t(m_at), m_bytes.begin() + ptrdiff_t(m_at + size));
        m_at += size;
        return bytes;
    }
    bool Reader::Raw(std::span<uint8_t> out)
    {
        if (!Need(out.size()))
            return false;
        std::memcpy(out.data(), m_bytes.data() + m_at, out.size());
        m_at += out.size();
        return true;
    }

    std::vector<uint8_t> Encode(const Hello& hello)
    {
        Writer w;
        w.U16(hello.protocol);
        w.U8(hello.host ? 1 : 0);
        w.Text(hello.launcherVersion);
        w.Text(hello.gameId);
        w.Text(hello.xexSha256);
        w.Text(hello.gameVersion);
        w.Text(hello.platform);
        w.U8(hello.crossPlatform ? 1 : 0);
        w.Raw(hello.nonce);
        return std::move(w.Bytes());
    }

    bool Decode(std::span<const uint8_t> payload, Hello& hello)
    {
        Reader r(payload);
        hello.protocol = r.U16();
        // A newer protocol may lay out the rest differently: only its number is read, for the message.
        if (r.Ok() && hello.protocol != LobbyProtocolVersion)
            return true;
        const uint8_t host = r.U8();
        hello.host = host == 1;
        hello.launcherVersion = r.Text(MaxText);
        hello.gameId = r.Text(MaxText);
        hello.xexSha256 = r.Text(MaxText);
        hello.gameVersion = r.Text(MaxText);
        hello.platform = r.Text(MaxText);
        const uint8_t cross = r.U8();
        hello.crossPlatform = cross == 1;
        r.Raw(hello.nonce);
        return r.AtEnd() && host <= 1 && cross <= 1;
    }

    std::vector<uint8_t> Encode(const Player& player)
    {
        Writer w;
        w.Text(player.pseudo);
        w.Text(player.xuid);
        w.U8(player.color);
        w.Blob(player.picture);
        w.U8(uint8_t(player.figures.size()));
        for (const Figure& figure : player.figures)
        {
            w.U16(figure.toyType);
            w.U16(figure.variant);
            w.U8(figure.seat);
            w.Text(figure.name);
            w.Blob(figure.dump);
        }
        return std::move(w.Bytes());
    }

    bool Decode(std::span<const uint8_t> payload, Player& player)
    {
        Reader r(payload);
        player.pseudo = r.Text(64);
        player.xuid = r.Text(16);
        player.color = r.U8();
        player.picture = r.Blob(MaxPictureBytes);
        const size_t count = r.U8();
        if (count > MaxFigures)
            return false;
        player.figures.clear();
        for (size_t i = 0; i < count && r.Ok(); ++i)
        {
            Figure figure;
            figure.toyType = r.U16();
            figure.variant = r.U16();
            figure.seat = r.U8();
            figure.name = r.Text(MaxText);
            figure.dump = r.Blob(2 * FigureDumpBytes);
            if (figure.dump.empty() || figure.dump.size() % FigureDumpBytes != 0)
                return false;
            player.figures.push_back(std::move(figure));
        }
        const bool xuidOk = player.xuid.size() == 16 && std::all_of(player.xuid.begin(), player.xuid.end(),
                                                            [](char c) { return std::isxdigit(uint8_t(c)); });
        return r.AtEnd() && xuidOk && !player.pseudo.empty();
    }

    std::vector<uint8_t> Encode(const Settings& settings)
    {
        Writer w;
        w.U8(settings.fpsLimit);
        w.Text(settings.language);
        w.Text(settings.voices);
        w.U8(settings.nightmareFromStart ? 1 : 0);
        w.U32(settings.saveBytes);
        w.U16(settings.saveFiles);
        return std::move(w.Bytes());
    }

    bool Decode(std::span<const uint8_t> payload, Settings& settings)
    {
        Reader r(payload);
        settings.fpsLimit = r.U8();
        settings.language = r.Text(16);
        settings.voices = r.Text(16);
        const uint8_t nightmare = r.U8();
        settings.nightmareFromStart = nightmare == 1;
        settings.saveBytes = r.U32();
        settings.saveFiles = r.U16();
        return r.AtEnd() && (settings.fpsLimit == 30 || settings.fpsLimit == 60) && nightmare <= 1 &&
               settings.saveBytes <= 64u * 1024 * 1024 && settings.saveFiles <= 256;
    }

    bool SafeSavePath(std::string_view path)
    {
        if (path.empty() || path.size() > 200 || path.front() == '/' || path.back() == '/')
            return false;
        size_t at = 0;
        while (at <= path.size())
        {
            const size_t slash = std::min(path.find('/', at), path.size());
            const auto part = path.substr(at, slash - at);
            if (part.empty() || part == "." || part == "..")
                return false;
            for (const char c : part)
                if (!(std::isalnum(uint8_t(c)) || c == '.' || c == '_' || c == '-' || c == ' '))
                    return false;
            at = slash + 1;
        }
        return true;
    }

    std::vector<uint8_t> Encode(const SaveChunk& chunk)
    {
        Writer w;
        w.Text(chunk.file);
        w.U32(chunk.fileBytes);
        w.U32(chunk.offset);
        w.Blob(chunk.bytes);
        return std::move(w.Bytes());
    }

    bool Decode(std::span<const uint8_t> payload, SaveChunk& chunk)
    {
        Reader r(payload);
        chunk.file = r.Text(MaxText);
        chunk.fileBytes = r.U32();
        chunk.offset = r.U32();
        chunk.bytes = r.Blob(SaveChunkBytes);
        return r.AtEnd() && SafeSavePath(chunk.file) && uint64_t(chunk.offset) + chunk.bytes.size() <= chunk.fileBytes;
    }

    std::vector<uint8_t> Encode(const Start& start)
    {
        Writer w;
        w.Raw(start.sessionKey);
        w.U8(start.inputDelay);
        w.U16(start.port);
        return std::move(w.Bytes());
    }

    bool Decode(std::span<const uint8_t> payload, Start& start)
    {
        Reader r(payload);
        r.Raw(start.sessionKey);
        start.inputDelay = r.U8();
        start.port = r.U16();
        return r.AtEnd() && start.inputDelay >= 1 && start.inputDelay <= 8 && start.port != 0;
    }

    Mismatch Compare(const Hello& mine, const Hello& theirs)
    {
        if (mine.protocol != theirs.protocol)
            return Mismatch::Protocol;
        if (mine.gameId != theirs.gameId)
            return Mismatch::Game;
        if (mine.xexSha256 != theirs.xexSha256)
            return Mismatch::Disc;
        if (mine.gameVersion != theirs.gameVersion)
            return Mismatch::GameVersion;
        if (mine.launcherVersion != theirs.launcherVersion)
            return Mismatch::Launcher;
        if (mine.platform != theirs.platform && !(mine.crossPlatform && theirs.crossPlatform))
            return Mismatch::Platform;
        return Mismatch::None;
    }

    std::array<uint8_t, 32> HmacSha256(std::span<const uint8_t> key, std::span<const uint8_t> message)
    {
        std::array<uint8_t, 64> block{};
        if (key.size() > block.size())
        {
            const auto hashed = Digest(key);
            std::copy(hashed.begin(), hashed.end(), block.begin());
        }
        else
            std::copy(key.begin(), key.end(), block.begin());
        std::array<uint8_t, 64> inner{}, outer{};
        for (size_t i = 0; i < block.size(); ++i)
        {
            inner[i] = uint8_t(block[i] ^ 0x36);
            outer[i] = uint8_t(block[i] ^ 0x5C);
        }
        const auto innerHash = Digest(inner, message);
        return Digest(outer, innerHash);
    }

    Keys DeriveKeys(uint64_t secret, const Nonce& hostNonce, const Nonce& guestNonce)
    {
        std::vector<uint8_t> salt;
        const auto label = AsBytes("arkeyan lobby v2");
        salt.insert(salt.end(), label.begin(), label.end());
        salt.insert(salt.end(), hostNonce.begin(), hostNonce.end());
        salt.insert(salt.end(), guestNonce.begin(), guestNonce.end());
        std::array<uint8_t, 5> secretBytes{};
        for (size_t i = 0; i < secretBytes.size(); ++i)
            secretBytes[i] = uint8_t(secret >> (8 * (4 - i)));
        const auto root = HmacSha256(secretBytes, salt);
        Keys keys;
        keys.hostToGuest = HmacSha256(root, AsBytes("host to guest"));
        keys.guestToHost = HmacSha256(root, AsBytes("guest to host"));
        keys.hostCipher = HmacSha256(root, AsBytes("encrypt host to guest"));
        keys.guestCipher = HmacSha256(root, AsBytes("encrypt guest to host"));
        return keys;
    }

    Tag ProofOf(const Keys& keys, bool host)
    {
        const auto mac =
            HmacSha256(host ? keys.hostToGuest : keys.guestToHost, AsBytes(host ? "proof host" : "proof guest"));
        Tag tag{};
        std::copy_n(mac.begin(), tag.size(), tag.begin());
        return tag;
    }

    namespace
    {
        Tag TagOf(
            MessageType type, uint32_t sequence, std::span<const uint8_t> payload, const std::array<uint8_t, 32>& key)
        {
            Writer header;
            header.U8(uint8_t(type));
            header.U32(sequence);
            header.Raw(payload);
            const auto mac = HmacSha256(key, header.Bytes());
            Tag tag{};
            std::copy_n(mac.begin(), tag.size(), tag.begin());
            return tag;
        }
    }

    namespace
    {
        CipherNonce FrameNonce(uint32_t sequence, bool fromHost)
        {
            CipherNonce nonce{};
            nonce[0] = fromHost ? 1 : 2;
            for (int i = 0; i < 4; ++i)
                nonce[4 + i] = uint8_t(sequence >> (8 * i));
            return nonce;
        }
    }

    std::vector<uint8_t> Frame(
        MessageType type, uint32_t sequence, std::span<const uint8_t> payload, const Keys* keys, bool fromHost)
    {
        std::vector<uint8_t> ciphertext(payload.begin(), payload.end());
        if (keys)
            ChaCha20Xor(fromHost ? keys->hostCipher : keys->guestCipher, FrameNonce(sequence, fromHost), 1, ciphertext);
        Writer w;
        w.U32(uint32_t(HeaderBytes + ciphertext.size() + TagBytes));
        w.U8(uint8_t(type));
        w.U32(sequence);
        w.Raw(ciphertext);
        const Tag tag =
            keys ? TagOf(type, sequence, ciphertext, fromHost ? keys->hostToGuest : keys->guestToHost) : Tag{};
        w.Raw(tag);
        return std::move(w.Bytes());
    }

    bool OpenFrame(Received& frame, const Keys& keys, bool fromHost)
    {
        if (!CheckTag(frame, fromHost ? keys.hostToGuest : keys.guestToHost))
            return false;
        return ChaCha20Xor(
            fromHost ? keys.hostCipher : keys.guestCipher, FrameNonce(frame.sequence, fromHost), 1, frame.payload);
    }

    void FrameReader::Feed(std::span<const uint8_t> bytes)
    {
        if (m_broken)
            return;
        // Keep the buffer from growing with frames already handed out.
        if (m_start > 0 && m_start == m_buffer.size())
        {
            m_buffer.clear();
            m_start = 0;
        }
        m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
    }

    std::optional<Received> FrameReader::Next()
    {
        if (m_broken || m_buffer.size() - m_start < 4)
            return std::nullopt;
        Reader length(std::span(m_buffer).subspan(m_start, 4));
        const size_t size = length.U32();
        if (size < HeaderBytes + TagBytes || size > MaxFrameBytes)
        {
            m_broken = true;
            return std::nullopt;
        }
        if (m_buffer.size() - m_start - 4 < size)
            return std::nullopt;
        Reader r(std::span(m_buffer).subspan(m_start + 4, size));
        Received frame;
        frame.type = MessageType(r.U8());
        frame.sequence = r.U32();
        const size_t payload = size - HeaderBytes - TagBytes;
        frame.payload.resize(payload);
        r.Raw(frame.payload);
        r.Raw(frame.tag);
        m_start += 4 + size;
        if (m_start > 1024 * 1024)
        {
            m_buffer.erase(m_buffer.begin(), m_buffer.begin() + ptrdiff_t(m_start));
            m_start = 0;
        }
        return frame;
    }

    bool CheckTag(const Received& frame, const std::array<uint8_t, 32>& key)
    {
        const Tag expected = TagOf(frame.type, frame.sequence, frame.payload, key);
        uint8_t difference = 0;
        for (size_t i = 0; i < expected.size(); ++i)
            difference |= uint8_t(expected[i] ^ frame.tag[i]);
        return difference == 0;
    }

    uint8_t InputDelayFor(double rttMs, double frameMs)
    {
        if (!(rttMs >= 0) || !(frameMs > 0))
            return 2;
        const double frames = std::ceil(rttMs / 2.0 / frameMs) + 1.0;
        return uint8_t(std::clamp(frames, 1.0, 8.0));
    }
}
