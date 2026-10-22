#include <EStudioUI/Application.h>
#include <EStudioUI/BackgroundTasks.h>

#include "ApplicationRuntime.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace EStudioUI
{
namespace
{
struct Runtime
{
    ThreadAffinity m_Affinity;
    std::vector<std::weak_ptr<Detail::ApplicationWindow>> m_Windows;
    std::vector<std::shared_ptr<void>> m_Retired;
    std::size_t m_DispatchDepth = 0;
    bool m_Processing = false;
    bool m_Running = false;
    bool m_ExplicitApplication = false;
    bool m_Quit = false;

    std::vector<std::shared_ptr<Detail::ApplicationWindow>> Snapshot()
    {
        std::vector<std::shared_ptr<Detail::ApplicationWindow>> windows;
        std::erase_if(m_Windows, [](const auto& entry) { return entry.expired(); });
        windows.reserve(m_Windows.size());
        for (const auto& entry : m_Windows)
            if (auto window = entry.lock())
                windows.push_back(std::move(window));
        return windows;
    }

    std::size_t OpenCount()
    {
        std::size_t count = 0;
        for (const auto& window : Snapshot())
            count += window->m_IsOpen() ? 1u : 0u;
        return count;
    }

    void CloseWindows()
    {
        m_Quit = true;
        for (const auto& window : Snapshot())
            window->m_Close();
    }

    double Process()
    {
        m_Affinity.VerifyAccess("Application event processing");
        if (m_Processing || m_DispatchDepth)
            throw std::logic_error("Application event processing cannot be nested inside a callback.");
        if (OpenCount() == 0)
        {
            Detail::FlushRetiredWindows();
            return -1;
        }

        struct ProcessingScope
        {
            Runtime& m_Runtime;

            explicit ProcessingScope(Runtime& runtime) : m_Runtime(runtime)
            {
                runtime.m_Processing = true;
            }

            ~ProcessingScope()
            {
                m_Runtime.m_Processing = false;
                Detail::FlushRetiredWindows();
            }
        } processing(*this);

        glfwPollEvents();
        double deadline = -1;
        for (const auto& window : Snapshot())
        {
            if (!window->m_IsOpen())
                continue;
            Detail::ApplicationDispatchScope scope;
            const double wake = window->m_Service();
            if (wake >= 0 && (deadline < 0 || wake < deadline))
                deadline = wake;
        }
        return deadline;
    }
};

Runtime& State()
{
    static Runtime runtime;
    return runtime;
}
} // namespace

namespace Detail
{
void VerifyApplicationAccess()
{
    State().m_Affinity.VerifyAccess("Native application access");
}

void RegisterApplicationWindow(const std::shared_ptr<ApplicationWindow>& window)
{
    VerifyApplicationAccess();
    State().m_Windows.push_back(window);
}

void RetireApplicationWindow(std::shared_ptr<void> window)
{
    VerifyApplicationAccess();
    State().m_Retired.push_back(std::move(window));
    FlushRetiredWindows();
}

void FlushRetiredWindows()
{
    auto& state = State();
    if (state.m_DispatchDepth || state.m_Processing || state.m_Retired.empty())
        return;
    auto retired = std::move(state.m_Retired);
    state.m_Retired.clear();
    retired.clear();
}

ApplicationDispatchScope::ApplicationDispatchScope()
{
    VerifyApplicationAccess();
    ++State().m_DispatchDepth;
}

ApplicationDispatchScope::~ApplicationDispatchScope()
{
    --State().m_DispatchDepth;
}
} // namespace Detail

Application::Application()
{
    Detail::VerifyApplicationAccess();
    auto& state = State();
    if (state.m_ExplicitApplication)
        throw std::logic_error("Only one explicit Application may own the native runtime.");
    state.m_ExplicitApplication = true;
}

Application::~Application()
{
    if (!m_Affinity.IsOwnerThread())
        std::terminate();
    State().CloseWindows();
    m_Tasks.reset();
    State().m_ExplicitApplication = false;
    Detail::FlushRetiredWindows();
}

void Application::VerifyAccess() const
{
    m_Affinity.VerifyAccess("Application access");
    Detail::VerifyApplicationAccess();
}

int Application::Run()
{
    VerifyAccess();
    return RunWindows();
}

int Application::RunWindows()
{
    Detail::VerifyApplicationAccess();
    auto& state = State();
    if (state.m_Running || state.m_Processing || state.m_DispatchDepth)
        throw std::logic_error("Application::Run cannot start a nested event loop.");

    struct RunScope
    {
        Runtime& m_Runtime;

        explicit RunScope(Runtime& runtime) : m_Runtime(runtime)
        {
            runtime.m_Running = true;
            runtime.m_Quit = false;
        }

        ~RunScope()
        {
            m_Runtime.m_Running = false;
            Detail::FlushRetiredWindows();
        }
    } running(state);

    while (!state.m_Quit && state.OpenCount())
    {
        const double deadline = state.Process();
        if (state.m_Quit || state.OpenCount() == 0)
            break;
        if (deadline >= 0)
            glfwWaitEventsTimeout(std::max(0.0001, deadline - glfwGetTime()));
        else
            glfwWaitEvents();
        Detail::FlushRetiredWindows();
    }
    return 0;
}

bool Application::ProcessEvents()
{
    VerifyAccess();
    State().Process();
    return State().OpenCount() != 0;
}

void Application::Quit()
{
    VerifyAccess();
    State().CloseWindows();
}

std::size_t Application::GetWindowCount() const
{
    VerifyAccess();
    return State().OpenCount();
}

bool Application::IsRunning() const
{
    VerifyAccess();
    return State().m_Running;
}

BackgroundTasks& Application::Tasks()
{
    VerifyAccess();
    if (!m_Tasks)
        m_Tasks = std::make_unique<BackgroundTasks>();
    return *m_Tasks;
}
} // namespace EStudioUI
