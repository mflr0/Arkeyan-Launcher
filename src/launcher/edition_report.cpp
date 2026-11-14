#include "launcher/edition_report.h"
#include "shared/collection/datadir.h"
#include <SDL3/SDL.h>
#include <string_view>

EditionReport RunEditionReport(const std::filesystem::path& builder, const std::string& image)
{
    const std::string program = datadir::Text(builder);
    const char* arguments[] = { program.c_str(), "--edition-report", image.c_str(), nullptr };
    SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, arguments);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_NULL);
#if defined(_WIN32)
    SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
#endif
    SDL_Process* process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (process == nullptr)
        return { false, "the game tools didn't start" };
    size_t size = 0;
    int code = -1;
    char* data = static_cast<char*>(SDL_ReadProcess(process, &size, &code));
    std::string output = data != nullptr ? std::string(data, size) : std::string();
    SDL_free(data);
    SDL_DestroyProcess(process);

    // The report is plain text; Arkeyan-Builder's events start with '@' ("@error why").
    std::string text, error;
    for (size_t at = 0; at < output.size();)
    {
        size_t end = output.find('\n', at);
        std::string_view line =
            std::string_view(output).substr(at, end == std::string::npos ? std::string::npos : end - at);
        at = end == std::string::npos ? output.size() : end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.starts_with("@error "))
            error = line.substr(7);
        else if (!line.starts_with("@"))
            text += std::string(line) + "\n";
    }
    if (code == 0 && text.starts_with("Edition report"))
        return { true, text };
    return { false, !error.empty() ? error : "the game tools couldn't read this disc" };
}

EditionReportJob::~EditionReportJob()
{
    if (m_thread.joinable())
        m_thread.join();
}

void EditionReportJob::Start(const std::filesystem::path& builder, const std::string& image)
{
    if (Running())
        return;
    if (m_thread.joinable())
        m_thread.join();
    {
        std::lock_guard lock(m_mutex);
        m_running = true;
        m_result.reset();
    }
    m_thread = std::thread([this, builder, image] {
        EditionReport report = RunEditionReport(builder, image);
        std::lock_guard lock(m_mutex);
        m_result = std::move(report);
        m_running = false;
    });
}

bool EditionReportJob::Running() const
{
    std::lock_guard lock(m_mutex);
    return m_running;
}

std::optional<EditionReport> EditionReportJob::Take()
{
    std::lock_guard lock(m_mutex);
    std::optional<EditionReport> result = std::move(m_result);
    m_result.reset();
    return result;
}
