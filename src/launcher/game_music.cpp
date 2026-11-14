#include "launcher/game_music.h"
#include "shared/art/game_art.h"
#include "shared/collection/datadir.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>

namespace gamemusic
{
    namespace
    {
        constexpr uint32_t OutputRate = 48000;
        constexpr const char* TrackName = "menu_music.wav";
        constexpr const char* NoneName = "menu_music.none";
        constexpr const char* CacheName = "menu_music.kit";

        uint32_t Le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
        uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

        // The fade's curve: equal power, so that two games crossing keep one loudness.
        float Curve(float level) { return std::sin(level * 1.5707963f); }

        float Toward(float value, float target, float step)
        {
            return value < target ? std::min(target, value + step) : std::max(target, value - step);
        }
    }

    std::shared_ptr<const Track> ParseWav(const std::vector<uint8_t>& file)
    {
        if (file.size() < 12 || memcmp(file.data(), "RIFF", 4) != 0 || memcmp(file.data() + 8, "WAVE", 4) != 0)
            return nullptr;
        auto track = std::make_shared<Track>();
        uint32_t channels = 0, loopStart = 0, loopEnd = 0;
        bool format = false, loop = false;
        std::vector<int16_t> samples;
        for (size_t at = 12; at + 8 <= file.size();)
        {
            const uint32_t size = Le32(&file[at + 4]);
            const size_t body = at + 8, available = std::min<size_t>(size, file.size() - body);
            if (memcmp(&file[at], "fmt ", 4) == 0 && available >= 16)
            {
                channels = Le16(&file[body + 2]);
                track->rate = Le32(&file[body + 4]);
                format = Le16(&file[body]) == 1 && Le16(&file[body + 14]) == 16 && (channels == 1 || channels == 2);
            }
            else if (memcmp(&file[at], "smpl", 4) == 0 && available >= 60 && Le32(&file[body + 28]) >= 1)
            {
                loopStart = Le32(&file[body + 44]);
                loopEnd = Le32(&file[body + 48]) + 1; // the chunk's end is inclusive
                loop = true;
            }
            else if (memcmp(&file[at], "data", 4) == 0 && format)
            {
                const size_t count = available / 2 / channels * channels;
                samples.resize(count);
                for (size_t i = 0; i < count; i++)
                    samples[i] = int16_t(Le16(&file[body + 2 * i]));
                break;
            }
            at = body + size + (size & 1);
        }
        if (!format || samples.empty() || track->rate == 0)
            return nullptr;
        if (channels == 1)
        {
            track->samples.resize(samples.size() * 2);
            for (size_t i = 0; i < samples.size(); i++)
                track->samples[2 * i] = track->samples[2 * i + 1] = samples[i];
        }
        else
            track->samples = std::move(samples);
        const uint32_t frames = track->Frames();
        if (!loop || loopEnd > frames || loopStart >= loopEnd)
            loopStart = 0, loopEnd = frames;
        track->loopStart = loopStart;
        track->loopEnd = loopEnd;
        return track;
    }

    std::shared_ptr<const Track> ReadWav(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return nullptr;
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return ParseWav(bytes);
    }

    std::filesystem::path File(const std::string& game)
    {
        std::filesystem::path folder = art::CacheFolder(game);
        return folder.empty() ? folder : folder / TrackName;
    }

    bool Extract(const std::string& game, const std::string& image, const std::filesystem::path& skyBuild,
        const std::string& kitVersion)
    {
        const std::filesystem::path file = File(game);
        std::error_code error;
        if (file.empty())
            return false;
        const bool cached = std::filesystem::is_regular_file(file, error) && ReadWav(file) != nullptr;
        const std::string cacheKey = "2:" + kitVersion; // packet-count traversal replaced the incomplete decoder
        const std::filesystem::path stamp = file.parent_path() / CacheName;
        {
            std::ifstream in(stamp);
            std::string remembered;
            if (cached && in && std::getline(in, remembered) && remembered == cacheKey)
                return true;
        }
        const std::filesystem::path none = file.parent_path() / NoneName;
        {
            std::ifstream in(none);
            std::string remembered;
            if (in && std::getline(in, remembered) && remembered == cacheKey)
                return cached;
        }
        if (skyBuild.empty() || !std::filesystem::is_regular_file(skyBuild, error))
            return cached; // keep a cached theme offline; a missing kit is retried at the next disc check
        std::filesystem::create_directories(file.parent_path(), error);

        const std::filesystem::path refresh = file.parent_path() / "menu_music.new.wav";
        std::filesystem::remove(refresh, error);
        const std::string program = datadir::Text(skyBuild), disc = image, out = datadir::Text(refresh);
        const char* arguments[] = { program.c_str(), "--iso", disc.c_str(), "--menu-music", out.c_str(), nullptr };
        SDL_PropertiesID properties = SDL_CreateProperties();
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arguments);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
#if defined(_WIN32)
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
        SDL_Process* process = SDL_CreateProcessWithProperties(properties);
        SDL_DestroyProperties(properties);
        int code = -1;
        if (process != nullptr)
        {
            SDL_WaitProcess(process, true, &code);
            SDL_DestroyProcess(process);
        }
        if (code == 0 && ReadWav(refresh) != nullptr && SDL_RenamePath(out.c_str(), datadir::Text(file).c_str()))
        {
            std::ofstream(stamp, std::ios::trunc) << cacheKey << "\n";
            std::filesystem::remove(none, error);
            return true;
        }
        std::filesystem::remove(refresh, error);
        std::ofstream(none, std::ios::trunc) << cacheKey << "\n";
        return cached;
    }

    void Mixer::Play(std::shared_ptr<const Track> track)
    {
        bool found = false;
        for (Voice& voice : m_voices)
        {
            voice.target = voice.track == track && track != nullptr ? 1.f : 0.f;
            found = found || voice.target > 0;
        }
        if (track != nullptr && !found)
            m_voices.push_back({ std::move(track), 0, 0, 1 });
    }

    void Mixer::Mute(bool muted, bool immediately)
    {
        m_masterTarget = muted ? 0.f : 1.f;
        if (immediately)
            m_master = m_masterTarget;
    }

    bool Mixer::Silent() const
    {
        if (m_master <= 0 && m_masterTarget <= 0)
            return true;
        return std::all_of(
            m_voices.begin(), m_voices.end(), [](const Voice& v) { return v.level <= 0 && v.target <= 0; });
    }

    float Mixer::GainOf(const std::shared_ptr<const Track>& track) const
    {
        for (const Voice& voice : m_voices)
            if (voice.track == track)
                return Curve(voice.level) * Curve(m_master) * Volume;
        return 0;
    }

    void Mixer::Render(float* out, uint32_t frames)
    {
        const float step = 1.f / (FadeSeconds * float(m_rate));
        for (uint32_t i = 0; i < frames; i++)
        {
            m_master = Toward(m_master, m_masterTarget, step);
            const float master = Curve(m_master) * Volume / 32768.f;
            for (Voice& voice : m_voices)
            {
                voice.level = Toward(voice.level, voice.target, step);
                if (voice.level <= 0)
                    continue;
                const Track& track = *voice.track;
                const uint32_t index = uint32_t(voice.position);
                const float fraction = float(voice.position - index);
                uint32_t next = index + 1;
                if (next >= track.loopEnd)
                    next = track.loopStart;
                const float gain = Curve(voice.level) * master;
                for (int c = 0; c < 2; c++)
                {
                    const float a = track.samples[2 * size_t(index) + c], b = track.samples[2 * size_t(next) + c];
                    out[2 * i + c] += (a + (b - a) * fraction) * gain;
                }
                voice.position += double(track.rate) / double(m_rate);
                const double length = double(track.loopEnd - track.loopStart);
                if (voice.position >= track.loopEnd)
                    voice.position = track.loopStart + std::fmod(voice.position - track.loopStart, length);
            }
        }
        m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                           [](const Voice& v) { return v.level <= 0 && v.target <= 0; }),
            m_voices.end());
    }

    struct Player::Shared
    {
        std::mutex mutex;
    };

    Player::Player() : m_mixer(std::make_unique<Mixer>(OutputRate)), m_shared(std::make_unique<Shared>()) {}

    Player::~Player()
    {
        if (m_stream != nullptr)
            SDL_DestroyAudioStream(m_stream); // closes the device; waits for the callback
    }

    Player::Loaded& Player::TrackOf(const std::string& game)
    {
        for (auto& [id, loaded] : m_tracks)
            if (id == game)
            {
                return loaded;
            }
        Loaded loaded;
        loaded.file = File(game);
        loaded.looked = SDL_GetTicks();
        std::error_code error;
        if (!loaded.file.empty() && std::filesystem::is_regular_file(loaded.file, error))
        {
            loaded.track = ReadWav(loaded.file);
            loaded.modified = std::filesystem::last_write_time(loaded.file, error);
        }
        m_tracks.emplace_back(game, std::move(loaded));
        return m_tracks.back().second;
    }

    bool Player::HasTrack(const std::string& game)
    {
        Loaded& loaded = TrackOf(game);
        // A disc check may create or replace the cached file. Keep it playable when the disc is absent.
        const uint64_t now = SDL_GetTicks();
        if (now - loaded.looked >= 1000)
        {
            loaded.looked = now;
            std::error_code error;
            if (!loaded.file.empty() && std::filesystem::is_regular_file(loaded.file, error))
            {
                const auto modified = std::filesystem::last_write_time(loaded.file, error);
                if (!error && (loaded.track == nullptr || modified != loaded.modified))
                {
                    loaded.track = ReadWav(loaded.file);
                    loaded.modified = modified;
                }
            }
        }
        return loaded.track != nullptr;
    }

    float Player::GainForTest(const std::string& game)
    {
        std::shared_ptr<const Track> track = TrackOf(game).track;
        std::lock_guard lock(m_shared->mutex);
        return m_mixer->GainOf(track);
    }

    void Player::Callback(void* user, SDL_AudioStream* stream, int additional, int)
    {
        Player& self = *static_cast<Player*>(user);
        std::vector<float> buffer;
        for (int left = additional; left > 0;)
        {
            const uint32_t frames = std::min<uint32_t>(uint32_t(left) / (2 * sizeof(float)), 2048);
            if (frames == 0)
                break;
            buffer.assign(size_t(frames) * 2, 0.f);
            {
                std::lock_guard lock(self.m_shared->mutex);
                self.m_mixer->Render(buffer.data(), frames);
            }
            SDL_PutAudioStreamData(stream, buffer.data(), int(frames * 2 * sizeof(float)));
            left -= int(frames * 2 * sizeof(float));
        }
    }

    bool IsPortalOutput(const std::string& name)
    {
        std::string lower = name;
        for (char& c : lower)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        for (const char* word : { "portal", "skylander", "spyro", "traptanium" })
            if (lower.find(word) != std::string::npos)
                return true;
        return false;
    }

    uint32_t ChooseOutput(const std::string& defaultName, const std::vector<std::pair<uint32_t, std::string>>& outputs)
    {
        if (!defaultName.empty() && !IsPortalOutput(defaultName))
            for (const auto& [id, name] : outputs)
                if (name == defaultName)
                    return id;
        for (const auto& [id, name] : outputs)
            if (!IsPortalOutput(name))
                return id;
        return 0;
    }

    namespace
    {
        std::string DefaultOutputName()
        {
            const char* name = SDL_GetAudioDeviceName(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK);
            return name != nullptr ? name : "";
        }
    }

    void Player::Open()
    {
        m_openFailed = true;
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
            return;
        // A physical device, never SDL's default: SDL moves a default-opened stream to a new default by itself, a portal too.
        std::vector<std::pair<uint32_t, std::string>> outputs;
        int count = 0;
        if (SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count))
        {
            for (int i = 0; i < count; i++)
            {
                const char* name = SDL_GetAudioDeviceName(ids[i]);
                outputs.emplace_back(ids[i], name != nullptr ? name : "");
            }
            SDL_free(ids);
        }
        m_defaultName = DefaultOutputName();
        if (outputs.empty()) // a driver that lists none: its default, unless that is a portal's
            outputs.emplace_back(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, m_defaultName);
        m_output = ChooseOutput(m_defaultName, outputs);
        m_paused = true;
        if (m_output == 0)
            return;
        const SDL_AudioSpec spec{ SDL_AUDIO_F32, 2, int(OutputRate) };
        m_stream = SDL_OpenAudioDeviceStream(m_output, &spec, &Player::Callback, this);
        m_openFailed = m_stream == nullptr;
    }

    void Player::Update(const std::string& game, bool muted)
    {
        std::shared_ptr<const Track> wanted;
        if (!game.empty() && HasTrack(game))
            wanted = TrackOf(game).track;
        if (m_stream == nullptr && wanted != nullptr && !m_openFailed)
            Open();
        // The default changed (a device plugged in, the system's settings): follow it, unless it is a portal's.
        if (m_stream != nullptr && SDL_GetTicks() >= m_nextCheck)
        {
            m_nextCheck = SDL_GetTicks() + 1000;
            if (DefaultOutputName() != m_defaultName)
            {
                SDL_DestroyAudioStream(m_stream);
                m_stream = nullptr;
                Open();
            }
        }
        if (m_stream == nullptr)
            return;
        bool silent;
        {
            std::lock_guard lock(m_shared->mutex);
            m_mixer->Play(wanted);
            m_mixer->Mute(muted, muted && m_paused);
            silent = m_mixer->Silent();
        }
        if (silent != m_paused)
        {
            m_paused = silent;
            if (silent)
            {
                SDL_PauseAudioStreamDevice(m_stream);
                SDL_ClearAudioStream(m_stream);
            }
            else
                SDL_ResumeAudioStreamDevice(m_stream);
        }
    }
}
