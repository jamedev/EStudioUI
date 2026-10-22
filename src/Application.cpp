#include <EStudioUI/Application.h>
#include <EStudioUI/BackgroundTasks.h>
#include <EStudioUI/StateStore.h>
#include <EStudioUI/InputEvent.h>

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
    struct Retired
    {
        std::shared_ptr<void> m_Owner;
        std::function<bool()> m_Ready;
    };

    ThreadAffinity m_Affinity;
    std::vector<std::weak_ptr<Detail::ApplicationWindow>> m_Windows;
    std::vector<Retired> m_Retired;
    std::size_t m_DispatchDepth = 0;
    bool m_Processing = false;
    bool m_Running = false;
    bool m_ExplicitApplication = false;
    bool m_Quit = false;
    bool m_Flushing = false;
    bool m_ClosingWindows = false;
    bool m_ShuttingDown = false;

    ~Runtime()
    {
        m_ShuttingDown = true;
        CloseWindows();
        while (HasNativeWork())
        {
            glfwPollEvents();
            glfwWaitEventsTimeout(.01);
            Detail::FlushRetiredWindows();
        }
        Detail::FlushRetiredWindows();
    }

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
        if (m_ClosingWindows)
            return;

        struct Scope
        {
            bool& m_Active;

            explicit Scope(bool& active) : m_Active(active)
            {
                m_Active = true;
            }

            ~Scope()
            {
                m_Active = false;
            }
        } closing(m_ClosingWindows);

        for (const auto& window : Snapshot())
            window->m_Close();
    }

    bool HasNativeWork()
    {
        for (const auto& window : Snapshot())
            if (window->m_HasPendingNativeWork && window->m_HasPendingNativeWork())
                return true;
        return false;
    }

    double Process()
    {
        m_Affinity.VerifyAccess("Application event processing");
        if (m_Processing || m_DispatchDepth || Detail::IsDeliveringInput())
            throw std::logic_error("Application event processing cannot be nested inside a callback.");
        if (OpenCount() == 0 && !HasNativeWork())
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

Runtime& GetRuntime()
{
    static Runtime runtime;
    return runtime;
}
} // namespace

namespace Detail
{
void VerifyApplicationAccess()
{
    GetRuntime().m_Affinity.VerifyAccess("Native application access");
}

void VerifyWindowCreation()
{
    VerifyApplicationAccess();
    if (GetRuntime().m_ShuttingDown || GetRuntime().m_ClosingWindows)
        throw std::logic_error("A native window cannot be created while the application is closing its windows.");
}

void RegisterApplicationWindow(const std::shared_ptr<ApplicationWindow>& window)
{
    VerifyApplicationAccess();
    GetRuntime().m_Windows.push_back(window);
}

void RetireApplicationWindow(std::shared_ptr<void> window, std::function<bool()> ready)
{
    VerifyApplicationAccess();
    GetRuntime().m_Retired.push_back({std::move(window), std::move(ready)});
    FlushRetiredWindows();
}

void FlushRetiredWindows()
{
    auto& state = GetRuntime();
    if (state.m_DispatchDepth || state.m_Processing || state.m_Flushing || state.m_Retired.empty())
        return;

    struct Scope
    {
        bool& m_Active;

        explicit Scope(bool& active) : m_Active(active)
        {
            m_Active = true;
        }

        ~Scope()
        {
            m_Active = false;
        }
    } flushing(state.m_Flushing);

    auto retired = std::move(state.m_Retired);
    state.m_Retired.clear();
    for (auto& entry : retired)
        if (entry.m_Ready && !entry.m_Ready())
            state.m_Retired.push_back(std::move(entry));
    retired.clear();
}

bool CanServiceNativeFrame()
{
    return GetRuntime().m_DispatchDepth == 1 && !IsDeliveringInput();
}

double ServiceNativeWindows()
{
    double deadline = -1;
    for (const auto& window : GetRuntime().Snapshot())
    {
        if (!window->m_IsOpen())
            continue;
        const ApplicationDispatchScope scope;
        const double wake = window->m_Service();
        if (wake >= 0 && (deadline < 0 || wake < deadline))
            deadline = wake;
    }
    return deadline;
}

ApplicationDispatchScope::ApplicationDispatchScope()
{
    VerifyApplicationAccess();
    ++GetRuntime().m_DispatchDepth;
}

ApplicationDispatchScope::~ApplicationDispatchScope()
{
    --GetRuntime().m_DispatchDepth;
}
} // namespace Detail

Application::Application()
{
    Detail::VerifyApplicationAccess();
    auto& state = GetRuntime();
    if (state.m_ExplicitApplication)
        throw std::logic_error("Only one explicit Application may own the native runtime.");
    state.m_ExplicitApplication = true;
}

Application::~Application()
{
    if (!m_Affinity.IsOwnerThread())
        std::terminate();
    GetRuntime().CloseWindows();
    m_Tasks.reset();
    GetRuntime().m_ExplicitApplication = false;
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
    if (StateStore::IsUpdatingOnThisThread())
        throw std::logic_error(
            "Application event processing cannot run inside a state update; defer it until the update finishes.");
    auto& state = GetRuntime();
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

    while ((!state.m_Quit && state.OpenCount()) || state.HasNativeWork())
    {
        const double deadline = state.Process();
        if ((state.m_Quit || state.OpenCount() == 0) && !state.HasNativeWork())
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
    if (StateStore::IsUpdatingOnThisThread())
        throw std::logic_error(
            "Application event processing cannot run inside a state update; defer it until the update finishes.");
    GetRuntime().Process();
    return GetRuntime().OpenCount() != 0 || GetRuntime().HasNativeWork();
}

void Application::Quit()
{
    VerifyAccess();
    GetRuntime().CloseWindows();
}

std::size_t Application::GetWindowCount() const
{
    VerifyAccess();
    return GetRuntime().OpenCount();
}

bool Application::IsRunning() const
{
    VerifyAccess();
    return GetRuntime().m_Running;
}

BackgroundTasks& Application::Tasks()
{
    VerifyAccess();
    if (!m_Tasks)
        m_Tasks = std::make_unique<BackgroundTasks>();
    return *m_Tasks;
}
} // namespace EStudioUI
