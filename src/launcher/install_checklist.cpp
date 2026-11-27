#include "launcher/install_checklist.h"
#include "launcher/i18n.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fmt/format.h>

namespace
{
    // Each step's usual seconds in a clean install, measured on a Windows PC with 16 threads and 15 compilers (the
    // first tester's, issue #139). The steps that use every compiler or every thread are scaled to this machine's.
    enum class Scale
    {
        None,
        Jobs,
        Threads
    };
    constexpr float TableThreads = 16, TableJobs = 15;
    struct KnownStep
    {
        const char* id;
        const char* title;
        Scale scale;
        float seconds[7]; // Spyro's Adventure, Giants, Swap Force, Trap Team, SuperChargers, Imaginators, other
    };
#ifdef _WIN32
    constexpr float CompilerSeconds = 30; // llvm-mingw's download, the first install only
#elif defined(__linux__)
    constexpr float CompilerSeconds = 10;
#else
    constexpr float CompilerSeconds = 2;
#endif
    constexpr KnownStep KnownSteps[] = {
        { "check", N_("Checking the disc"), Scale::None,
            { 5, 5, 5, 5, 5, 5, 5 } }, // only when the install waited for the image check
        { "kit", N_("Downloading the game tools"), Scale::None,
            { 20, 20, 20, 20, 20, 20, 20 } }, // only when the launcher fetched the kit itself
        { "toolchain", N_("Preparing the compiler"), Scale::None,
            { CompilerSeconds, CompilerSeconds, CompilerSeconds, CompilerSeconds, CompilerSeconds, CompilerSeconds,
                CompilerSeconds } },
        { "disc", N_("Reading the disc"), Scale::Jobs, { 1, 2, 1, 19, 1, 1, 4 } },
        { "recompile", N_("Translating the game code"), Scale::None, { 1, 1, 2, 1, 3, 3, 2 } },
        { "shaders", N_("Translating the shaders"), Scale::Threads, { 11, 59, 9, 8, 8, 8, 17 } },
        { "compile", N_("Compiling the game"), Scale::Jobs, { 34, 20, 67, 27, 93, 96, 56 } },
        { "link", N_("Linking the game"), Scale::None, { 1, 2, 1, 1, 1, 1, 1 } },
        { "movies", N_("Adding the movie shaders"), Scale::None, { 1, 8, 32, 7, 3, 3, 9 } },
        { "finish", N_("Finishing"), Scale::None, { 1, 3, 1, 1, 1, 1, 2 } },
        { "hdtextures", N_("Making the HD textures"), Scale::Threads,
            { 120, 120, 120, 120, 120, 120,
                120 } }, // only when asked for (Game settings > Video > Use HD textures): about 2 to 3 minutes
    };
    constexpr float UnknownSeconds = 4;

    // The steps whose time tells this machine's speed; the others wait on the network or the disc.
    bool MeasuresMachine(std::string_view id)
    {
        return id != "check" && id != "kit" && id != "toolchain" && id != "disc";
    }
}

InstallChecklist MakeInstallChecklist(const InstallProgress& progress, uint64_t now, std::string_view game)
{
    const size_t column = game == "ssa"             ? 0
                          : game == "giants"        ? 1
                          : game == "swapforce"     ? 2
                          : game == "trapteam"      ? 3
                          : game == "superchargers" ? 4
                          : game == "imaginators"   ? 5
                                                    : 6;
    const float threads = progress.threads > 0 ? float(progress.threads) : TableThreads,
                jobs = progress.jobs > 0 ? float(progress.jobs) : TableJobs;
    InstallChecklist list;
    std::vector<float> weights;
    auto has = [&](std::string_view id) {
        return std::any_of(progress.steps.begin(), progress.steps.end(),
            [&](const InstallProgress::Step& step) { return step.id == id; });
    };
    const bool downloadedKit = has("kit"), checkedDisc = has("check");
    for (const KnownStep& step : KnownSteps)
    {
        if ((!downloadedKit && std::string_view(step.id) == "kit") ||
            (!checkedDisc && std::string_view(step.id) == "check"))
            continue;
        if (!progress.hdTextures && std::string_view(step.id) == "hdtextures" &&
            std::none_of(progress.steps.begin(), progress.steps.end(),
                [](const InstallProgress::Step& s) { return s.id == "hdtextures"; }))
            continue;
        list.rows.push_back({ step.id, Tr(step.title) });
        const float scale = step.scale == Scale::Jobs      ? TableJobs / jobs
                            : step.scale == Scale::Threads ? TableThreads / threads
                                                           : 1;
        weights.push_back(step.seconds[column] * scale * 1000);
    }
    auto rowOf = [&](const std::string& id) -> int {
        for (size_t i = 0; i < list.rows.size(); i++)
            if (list.rows[i].id == id)
                return int(i);
        return -1;
    };

    // Each @step lands on its row; after "movies", the shaders/compile/link pass again: that is "finish".
    bool afterMovies = false;
    int last = -1, lastStepRow = -1;
    for (const InstallProgress::Step& step : progress.steps)
    {
        int row = afterMovies && (step.id == "shaders" || step.id == "compile" || step.id == "link") ? rowOf("finish")
                                                                                                     : rowOf(step.id);
        if (step.id == "movies")
            afterMovies = true;
        if (row < 0)
        {
            row = int(list.rows.size()) - 1; // before "finish"
            list.rows.insert(list.rows.begin() + row, { step.id, step.title });
            weights.insert(weights.begin() + row, UnknownSeconds * 1000);
        }
        InstallChecklist::Row& r = list.rows[size_t(row)];
        // A step seen again (the finish pass's several steps) adds up its time.
        bool timed = step.startedAt != 0 && (step.endedAt != 0 || progress.state == InstallProgress::State::Running);
        uint64_t end = step.endedAt != 0 ? step.endedAt : now;
        r.ms += timed && end > step.startedAt ? end - step.startedAt : 0;
        r.timed = r.timed || timed;
        r.state = InstallChecklist::RowState::Done;
        // Rows skipped between two steps (an older kit without one of them) count as done.
        for (int i = last + 1; i < row; i++)
            if (list.rows[size_t(i)].state == InstallChecklist::RowState::Waiting)
                list.rows[size_t(i)].state = InstallChecklist::RowState::Done;
        last = std::max(last, row);
        lastStepRow = row;
    }

    // The last step is the one running, or the one that failed or was stopped.
    using State = InstallProgress::State;
    if (!progress.steps.empty() && progress.state != State::Done)
    {
        int row = lastStepRow;
        list.current = row;
        InstallChecklist::Row& r = list.rows[size_t(row)];
        r.state = progress.state == State::Running  ? InstallChecklist::RowState::Running
                  : progress.state == State::Failed ? InstallChecklist::RowState::Failed
                                                    : InstallChecklist::RowState::Stopped;
        if (progress.total > 0)
        {
            r.done = std::min(progress.done, progress.total);
            r.total = progress.total;
            r.fraction = float(double(r.done) / double(r.total));
        }
        for (size_t i = size_t(row) + 1; i < list.rows.size(); i++)
            list.rows[i].state = InstallChecklist::RowState::Waiting;
    }
    else if (progress.state == State::Done)
        for (InstallChecklist::Row& r : list.rows)
            r.state = InstallChecklist::RowState::Done;
    else if (progress.state == State::Running)
        list.current = 0; // Arkeyan-Builder has started but said nothing yet

    // The overall fraction, by weight.
    float total = 0, done = 0;
    for (size_t i = 0; i < list.rows.size(); i++)
    {
        total += weights[i];
        if (list.rows[i].state == InstallChecklist::RowState::Done)
            done += weights[i];
        else if (int(i) == list.current && list.rows[i].fraction > 0)
            done += weights[i] * list.rows[i].fraction;
    }
    list.overall = progress.state == State::Done ? 1.0f : total > 0 ? std::clamp(done / total, 0.0f, 1.0f) : 0.0f;

    if (!progress.steps.empty() && progress.steps.front().startedAt != 0)
    {
        uint64_t end = progress.state == State::Running
                           ? now
                           : std::max(progress.steps.back().endedAt, progress.steps.back().startedAt);
        list.elapsedMs = end > progress.steps.front().startedAt ? end - progress.steps.front().startedAt : 0;
    }

    // Time left: each row's usual time (`weights`, in ms) at this machine's pace, the time the steps that measure it
    // took against their usual time. Ten seconds at the usual pace go in first, so that one short step does not swing
    // it. The current step goes by its own progress once it has some, else by its usual time. A rebuild gets no time
    // until a step has measured the machine: most of its steps may be up to date.
    if (progress.state == State::Running && list.current >= 0)
    {
        const InstallChecklist::Row& r = list.rows[size_t(list.current)];
        const double usual = weights[size_t(list.current)];
        const bool currentPace = MeasuresMachine(r.id) && r.fraction >= 0.02f && r.ms >= 2000;
        double measuredMs = 10000, measuredUsual = 10000;
        bool measured = false;
        if (currentPace)
        {
            measuredMs += double(r.ms);
            measuredUsual += usual * r.fraction;
            measured = true;
        }
        for (size_t i = 0; i < size_t(list.current); i++)
            if (list.rows[i].state == InstallChecklist::RowState::Done && list.rows[i].timed &&
                list.rows[i].ms >= 3000 && MeasuresMachine(list.rows[i].id))
            {
                measuredMs += double(list.rows[i].ms);
                measuredUsual += weights[i];
                measured = true;
            }
        if (measured || !progress.rebuild)
        {
            const double pace = std::clamp(measuredMs / measuredUsual, 0.3, 6.0);
            double left = currentPace ? double(r.ms) / r.fraction * (1 - r.fraction)
                                      : std::max(usual * pace - double(r.ms), usual * pace / 10);
            for (size_t i = size_t(list.current) + 1; i < list.rows.size(); i++)
                left += weights[i] * pace;
            list.etaMs = int64_t(left);
        }
    }

    // The current action: Arkeyan-Builder's last @log line in the step, else its count.
    if (progress.state == State::Running)
    {
        list.action = progress.action;
        if (list.action.empty() && progress.total > 0)
            list.action = Tf("{} of {}", progress.done, progress.total);
        if (list.action.empty())
            list.action = progress.steps.empty() ? Tr("Starting the installer") : Tr("Working");
    }
    return list;
}

std::string InstallTimeLeft(int64_t ms)
{
    if (ms < 0)
        return {};
    if (ms < 60000)
        return Tr("less than a minute left");
    int64_t minutes = (ms + 30000) / 60000;
    if (minutes < 60)
        return Tf("about {} min left", minutes);
    return Tf("about {0} h {1:02} min left", minutes / 60, minutes % 60);
}

std::string InstallDuration(uint64_t ms)
{
    uint64_t seconds = ms / 1000;
    if (seconds >= 3600)
        return fmt::format("{}:{:02}:{:02}", seconds / 3600, seconds / 60 % 60, seconds % 60);
    return fmt::format("{}:{:02}", seconds / 60, seconds % 60);
}

bool MissingMetalToolchain(const InstallProgress& progress)
{
    return progress.error.find("Metal shader compiler is missing") != std::string::npos ||
           progress.error.find("MetalToolchain") != std::string::npos;
}

std::string InstallFailureHeadline(const InstallChecklist& list, const InstallProgress& progress)
{
    if (MissingMetalToolchain(progress))
        return Tr("Xcode's Metal shader compiler isn't installed yet.");
    auto has = [&](const char* text) {
        std::string lower = progress.error;
        for (char& c : lower)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        return lower.find(text) != std::string::npos;
    };
    if (list.current >= 0 && list.rows[size_t(list.current)].id == "check")
        return Tr("The disc image is damaged.");
    if (progress.startFailed || has("kit wasn't found") || has("couldn't start"))
        return Tr("The installer couldn't start.");
    if (has("space") || has("no room") || has("disk full"))
        return Tr("Not enough free space: about 6 GB needed.");
    if (has("cannot write") || has("couldn't create") || has("permission") || has("access is denied"))
        return Tr("Couldn't write to the game folder.");
    std::string step = list.current >= 0 ? list.rows[size_t(list.current)].id : std::string();
    if (step == "kit")
        return Tr("Couldn't download the game tools. Check your connection.");
    if (step == "toolchain" && (has("download") || has("curl") || has("http") || has("sha-256") || has("checksum")))
        return Tr("Couldn't download the compiler. Check your connection.");
    if (step == "disc")
        return Tr("Couldn't read the disc image. Check that it's complete.");
    if (list.current >= 0)
    {
        std::string title = list.rows[size_t(list.current)].title;
        if (!title.empty())
            title[0] = char(std::tolower(static_cast<unsigned char>(title[0])));
        return Tf("Something went wrong while {}.", title);
    }
    return Tr("The installer stopped before the game was built.");
}
