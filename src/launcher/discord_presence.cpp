#include "launcher/discord_presence.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace discordpresence
{
    namespace
    {
        // The IPC's frames: a 32-bit opcode and a 32-bit length (little-endian), then that many bytes of JSON.
        enum Opcode : uint32_t { OpHandshake = 0, OpFrame = 1, OpClose = 2, OpPing = 3, OpPong = 4 };
        constexpr int ReadyTimeoutMs = 3000;
        constexpr auto RetryEvery = std::chrono::seconds(15);
        constexpr auto IdleWake = std::chrono::seconds(1);

        uint32_t ProcessId()
        {
#ifdef _WIN32
            return uint32_t(GetCurrentProcessId());
#else
            return uint32_t(getpid());
#endif
        }

        void AppendEscaped(std::string& out, std::string_view text)
        {
            out += '"';
            for (unsigned char c : text)
            {
                switch (c)
                {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                    {
                        char escaped[8];
                        std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
                        out += escaped;
                    }
                    else
                        out += char(c);
                }
            }
            out += '"';
        }

        // Discord's limit for each text: 128 bytes, cut at a character's start (UTF-8).
        std::string Clip(std::string text, size_t limit = 128)
        {
            if (text.size() <= limit)
                return text;
            size_t end = limit;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
                end--;
            text.resize(end);
            return text;
        }

        // One connection to the Discord app: discord-ipc-0 to -9, the first that answers.
        class Pipe
        {
        public:
            ~Pipe() { Close(); }
            bool Open() const
            {
#ifdef _WIN32
                return m_handle != INVALID_HANDLE_VALUE;
#else
                return m_fd >= 0;
#endif
            }

            bool Connect()
            {
                Close();
#ifdef _WIN32
                for (int i = 0; i < 10; ++i)
                {
                    wchar_t name[64];
                    std::swprintf(name, 64, L"\\\\.\\pipe\\discord-ipc-%d", i);
                    m_handle = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                    if (m_handle != INVALID_HANDLE_VALUE)
                        return true;
                }
                return false;
#else
                // Where each Discord build puts its socket: the system's own, Flatpak's and Snap's.
                std::vector<std::string> bases;
                for (const char* variable : { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" })
                    if (const char* value = std::getenv(variable); value != nullptr && *value != '\0')
                        bases.emplace_back(value);
                bases.emplace_back("/tmp");
                for (const std::string& base : bases)
                    for (const char* within : { "", "app/com.discordapp.Discord/", "snap.discord/", ".flatpak/dev.vencord.Vesktop/xdg-run/" })
                        for (int i = 0; i < 10; ++i)
                        {
                            std::string path = base;
                            if (!path.empty() && path.back() != '/')
                                path += '/';
                            path += within;
                            path += "discord-ipc-" + std::to_string(i);
                            if (TryConnect(path))
                                return true;
                        }
                return false;
#endif
            }

            void Close()
            {
#ifdef _WIN32
                if (m_handle != INVALID_HANDLE_VALUE)
                    CloseHandle(m_handle);
                m_handle = INVALID_HANDLE_VALUE;
#else
                if (m_fd >= 0)
                    ::close(m_fd);
                m_fd = -1;
#endif
                m_buffer.clear();
            }

            bool Send(uint32_t opcode, std::string_view json)
            {
                std::string frame(8, '\0');
                const uint32_t length = uint32_t(json.size());
                for (int i = 0; i < 4; ++i)
                {
                    frame[i] = char((opcode >> (8 * i)) & 0xFF);
                    frame[4 + i] = char((length >> (8 * i)) & 0xFF);
                }
                frame += json;
                return Write(frame);
            }

            // Reads what came within `timeoutMs` and takes out the complete frames: false when Discord closed the
            // connection (or said so with a Close frame). Pings are answered; `ready` is set by the READY event.
            bool Pump(int timeoutMs, bool* ready = nullptr)
            {
                if (!Read(timeoutMs))
                    return false;
                while (m_buffer.size() >= 8)
                {
                    uint32_t opcode = 0, length = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        opcode |= uint32_t(static_cast<unsigned char>(m_buffer[i])) << (8 * i);
                        length |= uint32_t(static_cast<unsigned char>(m_buffer[4 + i])) << (8 * i);
                    }
                    if (length > 64 * 1024)
                        return false; // not Discord
                    if (m_buffer.size() < 8 + size_t(length))
                        break;
                    const std::string body = m_buffer.substr(8, length);
                    m_buffer.erase(0, 8 + size_t(length));
                    if (opcode == OpClose)
                        return false;
                    if (opcode == OpPing && !Send(OpPong, body))
                        return false;
                    if (opcode == OpFrame && ready != nullptr && body.find("\"READY\"") != std::string::npos)
                        *ready = true;
                }
                return true;
            }

        private:
#ifdef _WIN32
            bool Write(const std::string& data)
            {
                DWORD written = 0;
                return WriteFile(m_handle, data.data(), DWORD(data.size()), &written, nullptr) && written == data.size();
            }

            bool Read(int timeoutMs)
            {
                for (int waited = 0;; waited += 20)
                {
                    DWORD available = 0;
                    if (!PeekNamedPipe(m_handle, nullptr, 0, nullptr, &available, nullptr))
                        return false;
                    if (available > 0)
                    {
                        std::string chunk(available, '\0');
                        DWORD read = 0;
                        if (!ReadFile(m_handle, chunk.data(), available, &read, nullptr))
                            return false;
                        m_buffer.append(chunk.data(), read);
                        return true;
                    }
                    if (waited >= timeoutMs)
                        return true;
                    Sleep(20);
                }
            }

            HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
            bool TryConnect(const std::string& path)
            {
                sockaddr_un address {};
                if (path.size() >= sizeof address.sun_path)
                    return false;
                address.sun_family = AF_UNIX;
                std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
                int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0)
                    return false;
#ifdef SO_NOSIGPIPE
                int on = 1;
                setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on); // macOS: a closed Discord is an error, not SIGPIPE
#endif
                if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0)
                {
                    ::close(fd);
                    return false;
                }
                m_fd = fd;
                return true;
            }

            bool Write(const std::string& data)
            {
#ifdef MSG_NOSIGNAL
                constexpr int Flags = MSG_NOSIGNAL;
#else
                constexpr int Flags = 0;
#endif
                size_t done = 0;
                while (done < data.size())
                {
                    ssize_t sent = ::send(m_fd, data.data() + done, data.size() - done, Flags);
                    if (sent < 0 && errno == EINTR)
                        continue;
                    if (sent <= 0)
                        return false;
                    done += size_t(sent);
                }
                return true;
            }

            bool Read(int timeoutMs)
            {
                pollfd entry { m_fd, POLLIN, 0 };
                int ready = ::poll(&entry, 1, timeoutMs);
                if (ready < 0)
                    return errno == EINTR;
                if (ready == 0)
                    return true;
                char chunk[4096];
                ssize_t got = ::recv(m_fd, chunk, sizeof chunk, 0);
                if (got <= 0)
                    return got < 0 && (errno == EINTR || errno == EAGAIN);
                m_buffer.append(chunk, size_t(got));
                return true;
            }

            int m_fd = -1;
#endif
            std::string m_buffer;
        };

        // Connects, introduces the application and waits for Discord's READY.
        bool Handshake(Pipe& pipe)
        {
            if (!pipe.Connect())
                return false;
            std::string hello = "{\"v\":1,\"client_id\":";
            AppendEscaped(hello, ApplicationId);
            hello += '}';
            if (!pipe.Send(OpHandshake, hello))
            {
                pipe.Close();
                return false;
            }
            bool ready = false;
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ReadyTimeoutMs);
            while (!ready && std::chrono::steady_clock::now() < until)
                if (!pipe.Pump(100, &ready))
                {
                    pipe.Close();
                    return false;
                }
            if (!ready)
                pipe.Close();
            return ready;
        }
    }

    std::string ActivityCommand(const Activity& activity, uint32_t pid, const std::string& nonce)
    {
        std::string json = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + ",\"activity\":";
        if (!activity.shown)
            json += "null";
        else
        {
            // status_display_type 2: the member list says "Playing <details>", the game's name, not the application's.
            json += "{\"type\":0";
            if (activity.namedByDetails)
                json += ",\"status_display_type\":2";
            json += ",\"details\":";
            AppendEscaped(json, Clip(activity.details));
            if (!activity.state.empty())
            {
                json += ",\"state\":";
                AppendEscaped(json, Clip(activity.state));
            }
            if (activity.start > 0)
                json += ",\"timestamps\":{\"start\":" + std::to_string(activity.start) + "}";
            json += ",\"assets\":{\"large_image\":";
            AppendEscaped(json, activity.largeImage);
            if (!activity.largeText.empty())
            {
                json += ",\"large_text\":";
                AppendEscaped(json, Clip(activity.largeText));
            }
            if (!activity.smallImage.empty())
            {
                json += ",\"small_image\":";
                AppendEscaped(json, activity.smallImage);
                if (!activity.smallText.empty())
                {
                    json += ",\"small_text\":";
                    AppendEscaped(json, Clip(activity.smallText));
                }
            }
            json += "},\"buttons\":[{\"label\":\"Arkeyan Launcher on GitHub\",\"url\":";
            AppendEscaped(json, RepositoryUrl);
            json += "}]}";
        }
        json += "},\"nonce\":";
        AppendEscaped(json, nonce);
        json += '}';
        return json;
    }

    Client::~Client()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
        }
        m_wake.notify_all();
        if (m_worker.joinable())
            m_worker.join();
    }

    void Client::Set(const Activity& activity)
    {
        {
            std::lock_guard lock(m_mutex);
            if (activity == m_activity)
                return;
            m_activity = activity;
            m_version++;
            if (!m_worker.joinable())
                m_worker = std::thread([this] { Run(); });
        }
        m_wake.notify_all();
    }

    void Client::Run()
    {
        Pipe pipe;
        uint64_t seen = 0, sent = 0, nonce = 0;
        bool showing = false; // what Discord was last told is not "nothing"
        auto nextTry = std::chrono::steady_clock::time_point {};
        const uint32_t pid = ProcessId();
        for (;;)
        {
            Activity activity;
            uint64_t version = 0;
            bool stop = false;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait_for(lock, IdleWake, [&] { return m_stop || m_version != seen; });
                activity = m_activity;
                version = seen = m_version;
                stop = m_stop;
            }
            if (stop)
            {
                if (pipe.Open() && showing)
                    pipe.Send(OpFrame, ActivityCommand({}, pid, std::to_string(++nonce)));
                return;
            }
            if (!pipe.Open())
            {
                if (!activity.shown)
                {
                    sent = version; // nothing to show and nothing to take back (Discord forgot it with the connection)
                    showing = false;
                    continue;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now < nextTry)
                    continue;
                nextTry = now + RetryEvery;
                if (!Handshake(pipe))
                    continue;
                sent = 0; // a new connection: the activity is sent again
            }
            if (!pipe.Pump(0))
            {
                pipe.Close();
                showing = false;
                continue;
            }
            if (sent != version)
            {
                if (!pipe.Send(OpFrame, ActivityCommand(activity, pid, std::to_string(++nonce))))
                {
                    pipe.Close();
                    showing = false;
                    continue;
                }
                sent = version;
                showing = activity.shown;
            }
        }
    }
}
