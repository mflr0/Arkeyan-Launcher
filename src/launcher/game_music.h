#pragma once

// The game's main menu theme on its page, very softly (docs/launcher_ui.md, "Game music"). The theme is never shipped: the
// kit's SkyBuild decodes it from the player's own disc into <data>/<Game>/launcher-cache/menu_music.wav at the disc check,
// next to the game's pictures, and this file plays it with SDL audio (a looping 16-bit WAV: its "smpl" chunk is the loop).
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct SDL_AudioStream;

namespace gamemusic
{
    constexpr float FadeSeconds = 1.5f; // in, out, and between two games
    constexpr float Volume = 0.2f;      // the theme's level under the page: low, a background

    struct Track
    {
        uint32_t rate = 0;
        std::vector<int16_t> samples;  // stereo, interleaved
        uint32_t loopStart = 0, loopEnd = 0; // in frames; loopEnd > loopStart
        uint32_t Frames() const { return uint32_t(samples.size() / 2); }
    };

    // A 16-bit PCM WAV of 1 or 2 channels (mono is doubled); its first "smpl" loop, else the whole file. Null if it is not one.
    std::shared_ptr<const Track> ParseWav(const std::vector<uint8_t>& file);
    std::shared_ptr<const Track> ReadWav(const std::filesystem::path& path);

    // <data>/<Game>/launcher-cache/menu_music.wav; empty when the game has no data folder.
    std::filesystem::path File(const std::string& game);

    // Makes File(game) with the kit's `skyBuild --menu-music` from the disc `image` (a disc image or a copy's folder), if it is
    // missing or was decoded by a different kit build. A kit that cannot (an older one, a game without a theme) is remembered by its `kitVersion` in
    // menu_music.none, so it is asked once, not at every start. True when the file exists afterwards. Blocks (~1 s): run it on
    // the disc check's thread.
    bool Extract(const std::string& game, const std::string& image, const std::filesystem::path& skyBuild, const std::string& kitVersion);

    // The fading mixer, apart from any device so that it can be checked: Play() makes `track` the one heard (fading in from
    // nothing, the others out, over FadeSeconds); Play(null) fades all out; Mute() fades everything out whatever is played.
    class Mixer
    {
    public:
        explicit Mixer(uint32_t outputRate) : m_rate(outputRate) {}
        void Play(std::shared_ptr<const Track> track);
        void Mute(bool muted, bool immediately = false);
        // `frames` stereo float frames, added into `out` (which the caller zeroed).
        void Render(float* out, uint32_t frames);
        bool Silent() const; // nothing audible and nothing to come
        float MasterGain() const { return m_master; }
        float GainOf(const std::shared_ptr<const Track>& track) const; // 0 when it is not playing

    private:
        struct Voice
        {
            std::shared_ptr<const Track> track;
            double position = 0; // in the track's frames
            float level = 0;     // 0..1, towards `target`
            float target = 0;
        };
        uint32_t m_rate;
        std::vector<Voice> m_voices;
        float m_master = 1, m_masterTarget = 1;
    };

    struct Playback
    {
        std::string game;
        bool gamePage = true;
        bool running = false;
        bool installing = false;
        bool launching = false;
        bool muted = false;
    };

    // The page's player: opens SDL audio at the first track it is given, nothing otherwise (a machine without a sound device
    // stays silent). Call Update() every frame from the UI thread.
    class Player
    {
    public:
        Player();
        ~Player();
        Player(const Player&) = delete;
        Player& operator=(const Player&) = delete;

        // `game`: the one whose page is shown, "" on the other pages and while a game runs; `muted`: the speaker's setting.
        void Update(const std::string& game, bool muted);
        void Update(const Playback& state)
        {
            Update(state.gamePage && !state.running && !state.installing && !state.launching ? state.game : std::string(), state.muted);
        }
        bool HasTrack(const std::string& game); // the game's file is there (read on first ask)
        Mixer& MixerForTest() { return *m_mixer; }
        bool FadeComplete() const { return m_paused; }
        bool DeviceOpen() const { return m_stream != nullptr; }
        float GainForTest(const std::string& game); // the game's track's loudness now (0: not playing)

    private:
        void Open();
        static void Callback(void* user, SDL_AudioStream* stream, int additional, int total);
        struct Loaded { std::shared_ptr<const Track> track; std::filesystem::path file; std::filesystem::file_time_type modified{}; uint64_t looked = 0; };
        std::vector<std::pair<std::string, Loaded>> m_tracks;
        Loaded& TrackOf(const std::string& game);
        std::unique_ptr<Mixer> m_mixer;
        struct Shared;
        std::unique_ptr<Shared> m_shared; // the mutex the callback and Update() share
        SDL_AudioStream* m_stream = nullptr;
        bool m_openFailed = false;
        bool m_paused = true;
    };
}
