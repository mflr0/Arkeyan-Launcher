#include "launcher/ui/picture_loader.h"
#include <algorithm>

namespace launcher_ui
{
    PictureLoader::PictureLoader() = default;

    PictureLoader::~PictureLoader()
    {
        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_stop = true;
            m_queue.clear();
        }
        m_wake.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    void PictureLoader::Request(const std::string& key, Job job)
    {
        {
            std::lock_guard<std::mutex> lock(m_lock);
            if (std::any_of(m_queue.begin(), m_queue.end(), [&](const Queued& queued) { return queued.key == key; }))
                return;
            m_queue.push_back({ key, std::move(job) });
            if (!m_thread.joinable()) // started on the first picture: most checks never ask for one
                m_thread = std::thread([this] { Run(); });
        }
        m_wake.notify_one();
    }

    void PictureLoader::Run()
    {
        std::unique_lock<std::mutex> lock(m_lock);
        for (;;)
        {
            m_wake.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            if (m_stop)
                return;
            Queued next = std::move(m_queue.back());
            m_queue.pop_back();
            m_running = next.key;
            m_runningForgotten = false;
            lock.unlock();
            DecodedPicture picture = next.job();
            lock.lock();
            m_decoded++;
            if (!m_runningForgotten)
                m_done.emplace_back(std::move(next.key), std::move(picture));
            m_running.clear();
            if (m_queue.empty())
                m_idle.notify_all();
        }
    }

    bool PictureLoader::Take(std::string& key, DecodedPicture& picture)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        if (m_done.empty())
            return false;
        key = std::move(m_done.front().first);
        picture = std::move(m_done.front().second);
        m_done.pop_front();
        return true;
    }

    bool PictureLoader::Pending() const
    {
        std::lock_guard<std::mutex> lock(m_lock);
        return !m_queue.empty() || !m_running.empty() || !m_done.empty();
    }

    void PictureLoader::Forget(const std::string& prefix)
    {
        std::lock_guard<std::mutex> lock(m_lock);
        auto starts = [&](const std::string& key) { return key.starts_with(prefix); };
        std::erase_if(m_queue, [&](const Queued& queued) { return starts(queued.key); });
        std::erase_if(m_done, [&](const auto& done) { return starts(done.first); });
        if (!m_running.empty() && starts(m_running))
            m_runningForgotten = true;
        if (m_queue.empty() && m_running.empty())
            m_idle.notify_all();
    }

    void PictureLoader::Wait()
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_idle.wait(lock, [this] { return m_queue.empty() && m_running.empty(); });
    }

    size_t PictureLoader::Decoded() const
    {
        std::lock_guard<std::mutex> lock(m_lock);
        return m_decoded;
    }
}
