#pragma once

// Pictures read, decoded, cropped and shrunk on a thread of their own (docs/launcher_ui.md, "Pictures"): the page asks
// for a picture by key and draws nothing in its place until it is ready; the UI thread only makes the finished ones
// into textures, a few per frame.
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace launcher_ui
{
    // A decoded picture: R, G, B, A bytes. Empty: there is none (the caller draws its own fallback).
    struct DecodedPicture
    {
        std::vector<uint8_t> rgba;
        int width = 0, height = 0;
        float aspect = 0; // the drawn part's width / height, when it is not the whole picture's
        bool generated = false; // drawn here (a figure without a disc picture), not read from a file

        bool Empty() const { return rgba.empty() || width <= 0 || height <= 0; }
    };

    class PictureLoader
    {
    public:
        using Job = std::function<DecodedPicture()>;

        PictureLoader();
        ~PictureLoader();
        PictureLoader(const PictureLoader&) = delete;
        PictureLoader& operator=(const PictureLoader&) = delete;

        // Queues `job` (run on the loader's thread) under `key`, unless that key is already waiting. The newest request
        // runs first: what was just scrolled into view.
        void Request(const std::string& key, Job job);
        // One finished picture, oldest first; false when none is ready.
        bool Take(std::string& key, DecodedPicture& picture);
        // Something is queued, running or finished and not taken yet.
        bool Pending() const;
        // Drops the queued and finished pictures whose key starts with `prefix`, and the running one's result.
        void Forget(const std::string& prefix);
        // Until nothing is queued or running (the screenshot test, the checks).
        void Wait();
        size_t Decoded() const; // jobs run so far (the checks: a picture is decoded once)

    private:
        struct Queued
        {
            std::string key;
            Job job;
        };
        void Run();

        mutable std::mutex m_lock;
        std::condition_variable m_wake, m_idle;
        std::deque<Queued> m_queue;
        std::deque<std::pair<std::string, DecodedPicture>> m_done;
        std::string m_running; // the key being decoded, empty when none
        bool m_runningForgotten = false, m_stop = false;
        size_t m_decoded = 0;
        std::thread m_thread;
    };
}
