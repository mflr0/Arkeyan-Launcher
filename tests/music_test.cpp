// ArkeyanLauncherTest's checks for the game page's music (launcher/game_music.h): the WAV reader, the mixer's fades and loop
// (no device), the extraction through the kit's Arkeyan-Builder (the fake one), and the player on SDL's dummy audio driver. Called from
// launcher_test.cpp's main; returns the number of failures. The real themes are checked in the workshop (ArkeyanBuilderMusicTest).
#include "test_env.h"
#include "launcher/game_music.h"
#include "launcher/launcher.h"
#include "launcher/game_settings.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <thread>

namespace
{
    int g_failures = 0;

#define MUSIC_EXPECT(condition) \
    do { if (!(condition)) { fprintf(stderr, "FAIL %s line %d: %s\n", __FILE__, __LINE__, #condition); g_failures++; } } while (0)

    // SKYLANDERS_DATA for a check, the old value back after.
    struct DataFolder
    {
        std::string old;
        bool had = false;
        explicit DataFolder(const std::filesystem::path& folder)
        {
            if (const char* value = SDL_getenv_unsafe("SKYLANDERS_DATA"))
                old = value, had = true;
            TestSetEnv("SKYLANDERS_DATA", folder.string().c_str());
        }
        ~DataFolder()
        {
            if (had)
                TestSetEnv("SKYLANDERS_DATA", old.c_str());
            else
                TestUnsetEnv("SKYLANDERS_DATA");
        }
    };

    void Put32(std::vector<uint8_t>& out, uint32_t v) { for (int i = 0; i < 4; i++) out.push_back(uint8_t(v >> (8 * i))); }
    void Put16(std::vector<uint8_t>& out, uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); }

    // A 16-bit WAV of `frames` frames at `rate`, every sample `value`, with a loop [start, end) when end > 0.
    std::vector<uint8_t> Wav(uint32_t rate, uint32_t channels, uint32_t frames, int16_t value, uint32_t loopStart = 0, uint32_t loopEnd = 0)
    {
        std::vector<uint8_t> f;
        auto tag = [&](const char* t) { f.insert(f.end(), t, t + 4); };
        const uint32_t data = frames * channels * 2, smpl = loopEnd > 0 ? 8 + 60 : 0;
        tag("RIFF"); Put32(f, 4 + 24 + smpl + 8 + data); tag("WAVE");
        tag("fmt "); Put32(f, 16); Put16(f, 1); Put16(f, uint16_t(channels)); Put32(f, rate); Put32(f, rate * channels * 2);
        Put16(f, uint16_t(channels * 2)); Put16(f, 16);
        if (loopEnd > 0)
        {
            tag("smpl"); Put32(f, 60);
            for (int i = 0; i < 7; i++) Put32(f, 0);
            Put32(f, 1); Put32(f, 0); Put32(f, 0); Put32(f, 0); Put32(f, loopStart); Put32(f, loopEnd - 1); Put32(f, 0); Put32(f, 0);
        }
        tag("data"); Put32(f, data);
        for (uint32_t i = 0; i < frames * channels; i++) Put16(f, uint16_t(value));
        return f;
    }

    // A ramp track: frame n holds n, so a position can be read back from the output.
    std::shared_ptr<const gamemusic::Track> Ramp(uint32_t frames, uint32_t rate, uint32_t loopStart, uint32_t loopEnd)
    {
        auto track = std::make_shared<gamemusic::Track>();
        track->rate = rate;
        track->samples.resize(size_t(frames) * 2);
        for (uint32_t i = 0; i < frames; i++)
            track->samples[2 * i] = track->samples[2 * i + 1] = int16_t(i);
        track->loopStart = loopStart;
        track->loopEnd = loopEnd;
        return track;
    }

    std::shared_ptr<const gamemusic::Track> Flat(int16_t value, uint32_t rate = 48000)
    {
        auto track = std::make_shared<gamemusic::Track>();
        track->rate = rate;
        track->samples.assign(size_t(rate) * 2 * 2, value); // 2 s
        track->loopEnd = rate * 2;
        return track;
    }

    // Renders `seconds` and returns the last frame's left sample.
    float Run(gamemusic::Mixer& mixer, double seconds)
    {
        const uint32_t frames = uint32_t(seconds * 48000);
        std::vector<float> out(size_t(frames) * 2, 0.f);
        mixer.Render(out.data(), frames);
        return out[size_t(frames) * 2 - 2];
    }

    void ReaderChecks()
    {
        auto mono = gamemusic::ParseWav(Wav(22050, 1, 100, 7));
        MUSIC_EXPECT(mono && mono->rate == 22050 && mono->Frames() == 100 && mono->samples[0] == 7 && mono->samples[1] == 7);
        MUSIC_EXPECT(mono && mono->loopStart == 0 && mono->loopEnd == 100); // no loop chunk: the whole file
        auto looped = gamemusic::ParseWav(Wav(44100, 2, 1000, -3, 200, 900));
        MUSIC_EXPECT(looped && looped->Frames() == 1000 && looped->samples[1999] == -3 && looped->loopStart == 200 && looped->loopEnd == 900);
        auto outside = gamemusic::ParseWav(Wav(44100, 2, 1000, 1, 200, 1200)); // a loop past the end: ignored
        MUSIC_EXPECT(outside && outside->loopStart == 0 && outside->loopEnd == 1000);
        MUSIC_EXPECT(gamemusic::ParseWav({}) == nullptr);
        MUSIC_EXPECT(gamemusic::ParseWav({ 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E' }) == nullptr);
        std::vector<uint8_t> eight = Wav(8000, 2, 10, 1);
        eight[34] = 8; // 8 bits per sample: not read
        MUSIC_EXPECT(gamemusic::ParseWav(eight) == nullptr);
        MUSIC_EXPECT(gamemusic::ReadWav("/no/such/folder/menu_music.wav") == nullptr);
    }

    void MixerChecks()
    {
        using gamemusic::FadeSeconds;
        using gamemusic::Volume;
        auto a = Flat(16384), b = Flat(16384);
        {
            // Fades in from nothing over FadeSeconds, then stays at the volume.
            gamemusic::Mixer mixer(48000);
            MUSIC_EXPECT(mixer.Silent());
            mixer.Play(a);
            MUSIC_EXPECT(!mixer.Silent());
            const float first = Run(mixer, 0.01);
            MUSIC_EXPECT(first > 0 && first < 0.1f * Volume * 0.5f);
            const float half = Run(mixer, FadeSeconds / 2 - 0.01);
            MUSIC_EXPECT(half > 0.5f * Volume * 0.5f * 0.5f && half < Volume * 0.5f); // the sample is 0.5 of full scale
            const float full = Run(mixer, FadeSeconds);
            MUSIC_EXPECT(std::fabs(full - Volume * 0.5f) < 1e-4f);
            MUSIC_EXPECT(std::fabs(mixer.GainOf(a) - Volume) < 1e-4f);
            // The same track asked again changes nothing; none fades out and the mixer ends silent.
            mixer.Play(a);
            MUSIC_EXPECT(std::fabs(Run(mixer, 0.1) - Volume * 0.5f) < 1e-4f);
            mixer.Play(nullptr);
            const float middle = Run(mixer, FadeSeconds / 2);
            MUSIC_EXPECT(middle > 0 && middle < Volume * 0.5f);
            Run(mixer, FadeSeconds);
            MUSIC_EXPECT(mixer.Silent() && mixer.GainOf(a) == 0);
        }
        {
            // Switching game: the new one in while the old one out, both at once for FadeSeconds, the loudness kept (equal power).
            gamemusic::Mixer mixer(48000);
            mixer.Play(a);
            Run(mixer, FadeSeconds + 0.1);
            mixer.Play(b);
            Run(mixer, FadeSeconds / 2);
            const float ga = mixer.GainOf(a), gb = mixer.GainOf(b);
            MUSIC_EXPECT(ga > 0 && gb > 0 && std::fabs(ga * ga + gb * gb - Volume * Volume) < 0.02f * Volume * Volume);
            Run(mixer, FadeSeconds);
            MUSIC_EXPECT(mixer.GainOf(a) == 0 && std::fabs(mixer.GainOf(b) - Volume) < 1e-4f);
            // Back to the first before the fade ended: the voice that is still there comes back (no restart from nothing).
            mixer.Play(a);
            mixer.Play(b);
            Run(mixer, 0.05);
            mixer.Play(a);
            MUSIC_EXPECT(mixer.GainOf(b) > 0.5f * Volume);
        }
        {
            // The speaker: muted fades everything out (the track still runs), unmuted brings it back.
            gamemusic::Mixer mixer(48000);
            mixer.Play(a);
            Run(mixer, FadeSeconds + 0.1);
            mixer.Mute(true);
            Run(mixer, FadeSeconds + 0.1);
            MUSIC_EXPECT(mixer.Silent() && mixer.GainOf(a) == 0 && mixer.MasterGain() == 0);
            mixer.Mute(false);
            MUSIC_EXPECT(!mixer.Silent());
            Run(mixer, FadeSeconds + 0.1);
            MUSIC_EXPECT(std::fabs(mixer.GainOf(a) - Volume) < 1e-4f);
            // Muted before anything plays: a track played then is silent, and unmuting brings it in.
            gamemusic::Mixer quiet(48000);
            quiet.Mute(true);
            Run(quiet, FadeSeconds + 0.1);
            quiet.Play(a);
            MUSIC_EXPECT(quiet.Silent() && Run(quiet, 0.5) == 0);
        }
        {
            // The loop: frames 100..199 of a ramp repeat, at the track's own rate (24 kHz played at 48 kHz: half a frame a step).
            auto ramp = Ramp(300, 24000, 100, 200);
            gamemusic::Mixer mixer(48000);
            mixer.Play(ramp);
            Run(mixer, FadeSeconds + 0.1);
            std::vector<float> out(4000, 0.f);
            mixer.Render(out.data(), 2000);
            float low = 1e9f, high = -1e9f;
            for (uint32_t i = 0; i < 2000; i++)
            {
                const float v = out[2 * i] * 32768.f / Volume; // the ramp's value (the fade is 1)
                low = std::min(low, v);
                high = std::max(high, v);
            }
            MUSIC_EXPECT(low >= 99.f && high <= 200.f); // never before the loop start, never past its end
            MUSIC_EXPECT(high > 190.f);                 // and it did wrap around within 2000 frames
        }
    }

    void ExtractChecks(const std::filesystem::path& scratch)
    {
        const std::filesystem::path data = scratch / "music-data";
        std::filesystem::remove_all(data);
        std::filesystem::create_directories(data);
        DataFolder dataFolder(data);
        const std::filesystem::path fake = std::filesystem::path(SKYLAUNCHER_FAKE_KIT) / "Arkeyan-Builder"
#if defined(_WIN32)
            ".exe"
#endif
            ;
        const std::string game = "ssa", disc = (scratch / "no-disc.iso").string();
        const std::filesystem::path file = gamemusic::File(game);
        MUSIC_EXPECT(!file.empty() && file.filename() == "menu_music.wav" && file.parent_path().filename() == "launcher-cache");
        MUSIC_EXPECT(!gamemusic::Extract(game, disc, {}, "1")); // no kit: nothing, and nothing remembered
        MUSIC_EXPECT(!std::filesystem::exists(file.parent_path() / "menu_music.none"));
        MUSIC_EXPECT(gamemusic::Extract(game, disc, fake, "1") && std::filesystem::is_regular_file(file));
        auto track = gamemusic::ReadWav(file);
        MUSIC_EXPECT(track && track->rate == 8000 && track->Frames() == 800);
        std::filesystem::remove(file);
        MUSIC_EXPECT(gamemusic::Extract("giants", disc, fake, "1")); // another game: its own file
        // A kit that cannot: remembered by its version, asked again only when the kit changes.
        std::filesystem::remove(gamemusic::File("giants"));
        SDL_setenv_unsafe("FAKE_SSABUILD_NO_MUSIC", "1", 1);
        MUSIC_EXPECT(!gamemusic::Extract(game, disc, fake, "2") && !std::filesystem::exists(file));
        MUSIC_EXPECT(std::filesystem::exists(file.parent_path() / "menu_music.none"));
        {
            std::ifstream in(file.parent_path() / "menu_music.none");
            std::string key;
            std::getline(in, key);
            MUSIC_EXPECT(key == "2:2");
        }
        SDL_unsetenv_unsafe("FAKE_SSABUILD_NO_MUSIC");
        MUSIC_EXPECT(!gamemusic::Extract(game, disc, fake, "2") && !std::filesystem::exists(file)); // same kit: not asked
        MUSIC_EXPECT(gamemusic::Extract(game, disc, fake, "3") && std::filesystem::is_regular_file(file)); // a newer kit: asked
        MUSIC_EXPECT(gamemusic::Extract(game, disc, {}, "3")); // there: kept, no kit needed
        auto cachedRate = [&] { auto cached = gamemusic::ReadWav(file); return cached ? cached->rate : 0; };
        // Old caches without provenance are refreshed, and later kit builds replace them atomically.
        std::filesystem::remove(file.parent_path() / "menu_music.kit");
        auto old = Wav(22050, 1, 100, 1);
        std::ofstream(file, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(old.data()), old.size());
        MUSIC_EXPECT(gamemusic::Extract(game, disc, fake, "3"));
        MUSIC_EXPECT(cachedRate() == 8000);
        std::ofstream(file, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(old.data()), old.size());
        MUSIC_EXPECT(gamemusic::Extract(game, disc, fake, "4"));
        MUSIC_EXPECT(cachedRate() == 8000);
        SDL_setenv_unsafe("FAKE_SSABUILD_NO_MUSIC", "1", 1);
        MUSIC_EXPECT(gamemusic::Extract(game, disc, fake, "5")); // a failed refresh retains the playable file
        MUSIC_EXPECT(cachedRate() == 8000);
        SDL_unsetenv_unsafe("FAKE_SSABUILD_NO_MUSIC");
        MUSIC_EXPECT(gamemusic::Extract(game, disc, fake, "6"));
    }

    void TransitionChecks(const std::filesystem::path& scratch)
    {
        const auto data = scratch / "music-transitions";
        DataFolder folder(data);
        for (const char* game : { "ssa", "giants", "swapforce", "trapteam" })
        {
            const auto path = gamemusic::File(game);
            std::filesystem::create_directories(path.parent_path());
            const auto wav = Wav(48000, 2, 96000, 8000);
            std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(wav.data()), wav.size());
        }
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        gamemusic::Player player;
        gamemusic::Playback state{ .game = "ssa", .muted = true };
        auto settle = [&] {
            player.Update(state);
            // Long enough for every fade on a busy CI machine (a fade is about 1 s; 1.9 s missed one on macOS CI).
            const uint64_t until = SDL_GetTicks() + 4000;
            while (SDL_GetTicks() < until)
            {
                SDL_Delay(20);
                player.Update(state);
            }
        };
        settle();
        MUSIC_EXPECT(player.FadeComplete() && player.GainForTest("ssa") == 0); // starts muted, no transient
        state.muted = false;
        settle();
        MUSIC_EXPECT(player.GainForTest("ssa") > gamemusic::Volume * 0.95f);
        state.game = "giants";
        player.Update(state);
        SDL_Delay(500);
        MUSIC_EXPECT(player.GainForTest("ssa") > 0 && player.GainForTest("giants") > 0);
        settle();
        MUSIC_EXPECT(player.GainForTest("ssa") == 0 && player.GainForTest("giants") > gamemusic::Volume * 0.95f);
        // Home, Collection and Settings all carry gamePage=false.
        for (int page = 0; page < 3; page++)
        {
            state.gamePage = false; settle();
            MUSIC_EXPECT(player.FadeComplete() && player.GainForTest("giants") == 0);
            state.gamePage = true; settle();
            MUSIC_EXPECT(player.GainForTest("giants") > gamemusic::Volume * 0.95f);
        }
        state.launching = true; settle();
        MUSIC_EXPECT(player.FadeComplete()); // only now may the process start
        state.launching = false; state.running = true; settle();
        MUSIC_EXPECT(player.FadeComplete());
        state.running = false; settle(); // normal exit
        MUSIC_EXPECT(player.GainForTest("giants") > gamemusic::Volume * 0.95f);
        state.running = true; settle();
        state.running = false; state.muted = true; settle(); // crash while muted
        MUSIC_EXPECT(player.FadeComplete());
        state.muted = false; settle();
        MUSIC_EXPECT(player.GainForTest("giants") > gamemusic::Volume * 0.95f);
        state.running = true; settle();
        state.running = false; settle(); // crash while its page is visible
        MUSIC_EXPECT(player.GainForTest("giants") > gamemusic::Volume * 0.95f);
        state.installing = true; settle();
        MUSIC_EXPECT(player.FadeComplete());
        state.installing = false; state.game = "swapforce"; settle();
        MUSIC_EXPECT(player.GainForTest("swapforce") > gamemusic::Volume * 0.95f);
        state.game = "trapteam"; settle();
        MUSIC_EXPECT(player.GainForTest("swapforce") == 0 && player.GainForTest("trapteam") > gamemusic::Volume * 0.95f);
        state.game = "unavailable"; settle();
        MUSIC_EXPECT(player.FadeComplete() && player.GainForTest("trapteam") == 0);
        std::filesystem::remove(gamemusic::File("ssa"));
        gamemusic::Player missing;
        missing.Update("ssa", false);
        MUSIC_EXPECT(!missing.HasTrack("ssa") && !missing.DeviceOpen());
        // One global preference survives loading the same launcher.ini in a new session.
        const auto ini = scratch / "music-setting.ini";
        LauncherConfig config;
        MUSIC_EXPECT(config.find("launcher") == config.end());
        config["launcher"]["game_music"] = "0";
        MUSIC_EXPECT(SaveLauncherConfig(ini.string(), config));
        MUSIC_EXPECT(LoadLauncherConfig(ini.string())["launcher"]["game_music"] == "0");
    }

    void PlayerChecks(const std::filesystem::path& scratch)
    {
        // The player on SDL's dummy audio driver: nothing is heard on this machine. A game without a file opens no device.
        const std::filesystem::path data = scratch / "music-player-data";
        std::filesystem::remove_all(data);
        std::filesystem::create_directories(data);
        DataFolder dataFolder(data);
        SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        {
            gamemusic::Player player;
            player.Update("ssa", false);
            MUSIC_EXPECT(!player.HasTrack("ssa") && !player.DeviceOpen());
            const std::filesystem::path file = gamemusic::File("ssa");
            std::filesystem::create_directories(file.parent_path());
            const std::vector<uint8_t> wav = Wav(48000, 2, 96000, 8000);
            std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(wav.data()), std::streamsize(wav.size()));
            // The file appears (the disc check made it): found within a second, without a restart.
            const auto start = std::chrono::steady_clock::now();
            while (!player.HasTrack("ssa") && std::chrono::steady_clock::now() - start < std::chrono::seconds(3))
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            MUSIC_EXPECT(player.HasTrack("ssa"));
            player.Update("ssa", false);
            MUSIC_EXPECT(player.DeviceOpen());
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            player.Update("ssa", false);
            const float gain = player.GainForTest("ssa");
            MUSIC_EXPECT(gain > 0 && gain < gamemusic::Volume); // fading in, in real time, through the device
            std::ofstream(file, std::ios::binary | std::ios::trunc) << "not a WAV";
            SDL_Delay(1100);
            MUSIC_EXPECT(!player.HasTrack("ssa")); // the existing cached track was replaced
            std::ofstream(file, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(wav.data()), wav.size());
            SDL_Delay(1100);
            MUSIC_EXPECT(player.HasTrack("ssa")); // refreshed music is read without restarting
            player.Update("", false); // another page: out
            player.Update("ssa", true);
        }
    }
}

int MusicChecks(const std::filesystem::path& scratch)
{
    g_failures = 0;
    ReaderChecks();
    MixerChecks();
    ExtractChecks(scratch);
    PlayerChecks(scratch);
    TransitionChecks(scratch);
    printf("music checks: %d failures\n", g_failures);
    return g_failures;
}
