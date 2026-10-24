#include "NativeResizePump.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#endif

namespace EStudioUI::Detail
{
struct NativeResizePump::Impl
{
    std::function<double()> m_Tick;
    std::exception_ptr m_Error;
#ifdef _WIN32
    HWND m_Window = nullptr;
    UINT_PTR m_Timer = 0;
    unsigned m_ModalDepth = 0;
    bool m_Ticking = false;
    bool m_Cancelled = false;

    void Stop()
    {
        if (m_Timer && m_Window)
            KillTimer(m_Window, m_Timer);
        m_Timer = 0;
    }

    void Tick() noexcept
    {
        if (m_Ticking || m_Error || m_Cancelled)
            return;
        m_Ticking = true;
        try
        {
            const double remaining = m_Tick();
            if (m_ModalDepth && m_Window && !m_Cancelled)
            {
                // Native timers are approximate and have a 10 ms floor. Size
                // callbacks also tick immediately, using the same presentation cap.
                const auto delay =
                    static_cast<UINT>(std::clamp(std::ceil(std::max(0.0, remaining) * 1000), 10.0, 50.0));
                const auto timer =
                    SetTimer(m_Window, reinterpret_cast<UINT_PTR>(this), remaining < 0 ? 17u : delay, nullptr);
                if (!timer)
                    throw std::runtime_error("Could not schedule the native resize timer.");
                m_Timer = timer;
            }
        }
        catch (...)
        {
            m_Error = std::current_exception();
            Stop();
        }
        m_Ticking = false;
    }

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto& self = *reinterpret_cast<Impl*>(data);
        if (message == WM_TIMER && wParam == self.m_Timer && self.m_Timer)
        {
            self.Tick();
            return 0;
        }
        if (message == WM_ENTERSIZEMOVE || message == WM_ENTERMENULOOP)
        {
            ++self.m_ModalDepth;
            const auto result = DefSubclassProc(window, message, wParam, lParam);
            self.Tick();
            return result;
        }
        if (message == WM_EXITSIZEMOVE || message == WM_EXITMENULOOP)
        {
            if (self.m_ModalDepth)
                --self.m_ModalDepth;
            if (!self.m_ModalDepth)
                self.Stop();
            const auto result = DefSubclassProc(window, message, wParam, lParam);
            self.Tick();
            return result;
        }
        if (message == WM_NCDESTROY)
        {
            self.Stop();
            RemoveWindowSubclass(window, Procedure, reinterpret_cast<UINT_PTR>(&self));
            self.m_Window = nullptr;
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }
#endif
};

NativeResizePump::NativeResizePump(std::function<double()> tick) : m_Impl(std::make_unique<Impl>())
{
    if (!tick)
        throw std::invalid_argument("A native resize pump requires a callback.");
    m_Impl->m_Tick = std::move(tick);
}

NativeResizePump::~NativeResizePump()
{
#ifdef _WIN32
    m_Impl->m_Cancelled = true;
    m_Impl->Stop();
    if (m_Impl->m_Window)
        RemoveWindowSubclass(m_Impl->m_Window, Impl::Procedure, reinterpret_cast<UINT_PTR>(m_Impl.get()));
#endif
}

void NativeResizePump::Attach(void* nativeWindow)
{
#ifdef _WIN32
    if (m_Impl->m_Window || !nativeWindow)
        throw std::logic_error("Attach a native resize pump exactly once to a live HWND.");
    const auto window = static_cast<HWND>(nativeWindow);
    if (!SetWindowSubclass(window, Impl::Procedure, reinterpret_cast<UINT_PTR>(m_Impl.get()),
                           reinterpret_cast<DWORD_PTR>(m_Impl.get())))
        throw std::runtime_error("Could not install the native resize scheduler.");
    m_Impl->m_Window = window;
#else
    (void)nativeWindow;
#endif
}

void NativeResizePump::RethrowError() const
{
    if (m_Impl->m_Error)
        std::rethrow_exception(m_Impl->m_Error);
}

void NativeResizePump::Cancel()
{
#ifdef _WIN32
    m_Impl->m_Cancelled = true;
    m_Impl->Stop();
    if (m_Impl->m_ModalDepth && m_Impl->m_Window)
        PostMessageW(m_Impl->m_Window, WM_CANCELMODE, 0, 0);
#endif
}
} // namespace EStudioUI::Detail
