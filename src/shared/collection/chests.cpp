#include "chests.h"
#include "save_backup.h"
#include <algorithm>
#include <charconv>

namespace chests
{
    namespace
    {
        constexpr std::string_view RequestFile = "chests_add.txt", ClaimedFile = "chests_add.claimed",
                                   StatusFile = "chests.txt";

        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        // A whole decimal number, or nothing.
        std::optional<long long> Number(std::string_view text)
        {
            long long value = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error == std::errc::result_out_of_range && !text.empty() && text.front() != '-')
                return PileLimit;
            if (error != std::errc() || end != text.data() + text.size())
                return std::nullopt;
            return value;
        }

        int Count(std::string_view text)
        {
            const std::optional<long long> value = Number(text);
            return value ? int(std::clamp<long long>(*value, 0, PileLimit)) : 0;
        }

        // Calls `line(key, value)` for each `key=value` line.
        template <typename Line>
        void EachLine(std::string_view text, Line line)
        {
            while (!text.empty())
            {
                const size_t end = text.find('\n');
                const std::string_view row = text.substr(0, end);
                text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
                const size_t equals = row.find('=');
                if (equals != std::string_view::npos)
                    line(Trim(row.substr(0, equals)), Trim(row.substr(equals + 1)));
            }
        }

        int KindIndex(std::string_view key, std::span<const std::string_view> kinds)
        {
            const auto found = std::find(kinds.begin(), kinds.end(), key);
            return found == kinds.end() ? -1 : int(found - kinds.begin());
        }

        int Slot(std::string_view text)
        {
            const std::optional<long long> value = Number(text);
            return value && *value >= 0 && *value < 1000 ? int(*value) : -1;
        }
    }

    int Request::Total() const
    {
        int total = 0;
        for (int count : counts)
            total += count;
        return total;
    }

    int Status::Total() const
    {
        int total = other;
        for (int count : pile)
            total += count;
        return total;
    }

    std::optional<Request> ParseRequest(std::string_view text, std::span<const std::string_view> kinds)
    {
        Request request;
        request.counts.assign(kinds.size(), 0);
        EachLine(text, [&](std::string_view key, std::string_view value) {
            if (key == "slot")
                request.slot = Slot(value);
            else if (const int kind = KindIndex(key, kinds); kind >= 0)
                request.counts[size_t(kind)] = Count(value);
        });
        if (request.slot < 0)
            return std::nullopt;
        return request;
    }

    std::string FormatRequest(const Request& request, std::span<const std::string_view> kinds)
    {
        std::string text = "slot=" + std::to_string(request.slot) + "\n";
        for (size_t i = 0; i < kinds.size(); i++)
            text +=
                std::string(kinds[i]) + "=" + std::to_string(i < request.counts.size() ? request.counts[i] : 0) + "\n";
        return text;
    }

    std::optional<Status> ParseStatus(std::string_view text, std::span<const std::string_view> kinds)
    {
        Status status;
        status.pile.assign(kinds.size(), 0);
        EachLine(text, [&](std::string_view key, std::string_view value) {
            if (key == "slot")
                status.slot = Slot(value);
            else if (key == "pile.other")
                status.other = Count(value);
            else if (key.starts_with("pile."))
            {
                if (const int kind = KindIndex(key.substr(5), kinds); kind >= 0)
                    status.pile[size_t(kind)] = Count(value);
            }
        });
        if (status.slot < 0)
            return std::nullopt;
        return status;
    }

    std::string FormatStatus(const Status& status, std::span<const std::string_view> kinds, int limit)
    {
        std::string text = "slot=" + std::to_string(status.slot) + "\npile=" + std::to_string(status.Total()) + "\n";
        for (size_t i = 0; i < kinds.size(); i++)
            text += "pile." + std::string(kinds[i]) + "=" +
                    std::to_string(i < status.pile.size() ? status.pile[i] : 0) + "\n";
        text += "pile.other=" + std::to_string(status.other) + "\nlimit=" + std::to_string(limit) + "\n";
        return text;
    }

    int RoomLeft(int pile, int waiting, int limit)
    {
        return std::max(0, limit - std::max(0, pile) - std::max(0, waiting));
    }

    std::vector<int> Plan(const Request& request, int pile, int limit)
    {
        int room = RoomLeft(pile, 0, limit);
        std::vector<int> plan;
        plan.reserve(request.counts.size());
        for (int count : request.counts)
        {
            const int add = std::clamp(count, 0, room);
            plan.push_back(add);
            room -= add;
        }
        return plan;
    }

    bool Folder::WriteStatus(const Status& status, std::span<const std::string_view> kinds)
    {
        if (m_folder.empty())
            return false;
        std::string text = FormatStatus(status, kinds);
        if (text == m_written)
            return false;
        std::error_code error;
        std::filesystem::create_directories(m_folder, error);
        if (error || !savebackup::WriteFileAtomic(m_folder / StatusFile, text.data(), text.size()))
            return false;
        m_written = std::move(text);
        return true;
    }

    std::optional<Request> Folder::Claim(int slot, std::span<const std::string_view> kinds)
    {
        if (m_folder.empty())
            return std::nullopt;
        const std::filesystem::path waiting = m_folder / RequestFile, claimed = m_folder / ClaimedFile;
        std::error_code error;
        if (!std::filesystem::exists(waiting, error))
            return std::nullopt;
        const std::optional<Request> peek = ParseRequest(savebackup::ReadFile(waiting), kinds);
        if (!peek || peek->slot != slot)
            return std::nullopt;
        std::filesystem::rename(waiting, claimed, error);
        if (error)
            return std::nullopt;
        // Read again: the launcher may have replaced the file between the first read and the rename.
        std::optional<Request> request = ParseRequest(savebackup::ReadFile(claimed), kinds);
        if (request && request->slot != slot)
        {
            // Put back unless the launcher wrote yet another one meanwhile (the newer one wins).
            if (!std::filesystem::exists(waiting, error))
                std::filesystem::rename(claimed, waiting, error);
            std::filesystem::remove(claimed, error);
            return std::nullopt;
        }
        std::filesystem::remove(claimed, error);
        return request;
    }
}
