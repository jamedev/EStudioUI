#include "NativeWindowChrome.h"

#include <exception>
#include <stdexcept>
#include <algorithm>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#endif

namespace EStudioUI::Detail
{
struct NativeWindowChrome::Impl : std::enable_shared_from_this<Impl>
{
    std::weak_ptr<void> m_Owner;
    std::function<WindowHitRegion(Point)> m_HitTest;
    std::function<void(PointerEvent)> m_Pointer;
    std::exception_ptr m_Error;
    bool m_Enabled = false;
#ifdef _WIN32
    HWND m_Window = nullptr;
    bool m_CompositionEnabled = false;
    bool m_CaptionCaptured = false;
    Point m_LastPosition;

    static bool IsCaptionButton(WPARAM region)
    {
        return region == HTMINBUTTON || region == HTMAXBUTTON || region == HTCLOSE;
    }

    void Extend()
    {
        BOOL composed = FALSE;
        m_CompositionEnabled = SUCCEEDED(DwmIsCompositionEnabled(&composed)) && composed;
        if (m_CompositionEnabled)
        {
            const MARGINS margins{0, 0, m_Enabled ? 1 : 0, 0};
            DwmExtendFrameIntoClientArea(m_Window, &margins);
        }
        // Client geometry alone does not stop DWM from painting caption controls
        // after a title/activation update. Keep shell styles for snapping and the
        // system menu, but give this GPU window exclusive ownership of its frame.
        const DWMNCRENDERINGPOLICY policy = m_Enabled ? DWMNCRP_DISABLED : DWMNCRP_USEWINDOWSTYLE;
        const auto result = DwmSetWindowAttribute(m_Window, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
        if (m_Enabled && m_CompositionEnabled && FAILED(result))
        {
            throw std::runtime_error("Windows could not disable the system caption renderer.");
        }
    }

    Point ClientPoint(LPARAM position, bool screen) const
    {
        POINT point{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        if (screen)
        {
            ScreenToClient(m_Window, &point);
        }
        return {static_cast<float>(point.x), static_cast<float>(point.y)};
    }

    void Pointer(Point position, PointerEventType type, bool held)
    {
        m_LastPosition = position;
        PointerEvent event{};
        event.m_Position = position;
        event.m_Type = type;
        event.m_Button = type == PointerEventType::Move ? PointerButton::None : PointerButton::Left;
        event.m_Buttons = held ? 1u : 0u;
        event.m_PreserveFocus = true;
        if (m_Pointer && !m_Owner.expired())
        {
            // Keep the callable alive if delivery retires its window or this adapter.
            const auto deliver = m_Pointer;
            deliver(event);
        }
    }

    void ReleaseCaption()
    {
        m_CaptionCaptured = false;
        if (m_Window && GetCapture() == m_Window)
        {
            ReleaseCapture();
        }
    }

    void CancelCaption()
    {
        if (m_CaptionCaptured)
        {
            ReleaseCaption();
            Pointer(m_LastPosition, PointerEventType::Cancel, false);
        }
    }

    void CalculateClient(WPARAM extended, LPARAM parameters)
    {
        auto& rectangle =
            extended ? reinterpret_cast<NCCALCSIZE_PARAMS*>(parameters)->rgrc[0] : *reinterpret_cast<RECT*>(parameters);
        const RECT proposed = rectangle;
        // Let User32 maintain its frame bookkeeping, then replace the caption inset.
        // Its WVR_* result describes different coordinates and can copy stale caption pixels.
        DefSubclassProc(m_Window, WM_NCCALCSIZE, extended, parameters);
        const UINT dpi = GetDpiForWindow(m_Window);
        const LONG border =
            GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        rectangle.top = proposed.top + (IsZoomed(m_Window) ? border : 1);
        if (IsZoomed(m_Window))
        {
            const auto monitor = MonitorFromRect(&proposed, MONITOR_DEFAULTTONEAREST);
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            if (GetMonitorInfoW(monitor, &info))
            {
                // Preserve reveal edges for auto-hidden app bars on every monitor.
                for (UINT edge = ABE_LEFT; edge <= ABE_BOTTOM; ++edge)
                {
                    APPBARDATA bar{};
                    bar.cbSize = sizeof(bar);
                    bar.uEdge = edge;
                    bar.rc = info.rcMonitor;
                    if (!SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar))
                    {
                        continue;
                    }
                    if (edge == ABE_LEFT)
                    {
                        rectangle.left += 2;
                    }
                    else if (edge == ABE_TOP)
                    {
                        rectangle.top += 2;
                    }
                    else if (edge == ABE_RIGHT)
                    {
                        rectangle.right -= 2;
                    }
                    else
                    {
                        rectangle.bottom -= 2;
                    }
                }
            }
        }
        // Minimize and very small native frames can be smaller than their non-client insets.
        rectangle.left = std::clamp(rectangle.left, proposed.left, proposed.right);
        rectangle.right = std::clamp(rectangle.right, rectangle.left, proposed.right);
        rectangle.bottom = std::clamp(rectangle.bottom, proposed.top, proposed.bottom);
        rectangle.top = std::clamp(rectangle.top, proposed.top, rectangle.bottom);
    }

    LRESULT HitTest(LPARAM position) const
    {
        POINT screen{GET_X_LPARAM(position), GET_Y_LPARAM(position)};
        RECT bounds{};
        GetWindowRect(m_Window, &bounds);
        POINT client = screen;
        ScreenToClient(m_Window, &client);
        RECT content{};
        GetClientRect(m_Window, &content);
        const UINT dpi = GetDpiForWindow(m_Window);
        const int bx = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        const int by = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        const auto hitTest = m_HitTest;
        const auto region = hitTest({static_cast<float>(client.x), static_cast<float>(client.y)});
        // Visible controls retain their complete clickable rectangle, including its top edge.
        if (PtInRect(&content, client) && region != WindowHitRegion::Caption)
        {
            if (region == WindowHitRegion::Minimize)
            {
                return HTMINBUTTON;
            }
            if (region == WindowHitRegion::Maximize)
            {
                return HTMAXBUTTON;
            }
            if (region == WindowHitRegion::Close)
            {
                return HTCLOSE;
            }
            return HTCLIENT;
        }
        if (!IsZoomed(m_Window))
        {
            const bool left = screen.x < bounds.left + bx, right = screen.x >= bounds.right - bx;
            const bool top = screen.y < bounds.top + by, bottom = screen.y >= bounds.bottom - by;
            if (top)
            {
                return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
            }
            if (bottom)
            {
                return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
            }
            if (left || right)
            {
                return left ? HTLEFT : HTRIGHT;
            }
        }
        // DWM's native caption rectangles do not describe our GPU controls.
        switch (region)
        {
        case WindowHitRegion::Caption:
            return HTCAPTION;
        case WindowHitRegion::Minimize:
            return HTMINBUTTON;
        case WindowHitRegion::Maximize:
            return HTMAXBUTTON;
        case WindowHitRegion::Close:
            return HTCLOSE;
        case WindowHitRegion::Client:
            return HTCLIENT;
        }
        return HTCLIENT;
    }

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto& self = *reinterpret_cast<Impl*>(data);
        const auto retained = self.shared_from_this();
        const auto owner = self.m_Owner.lock();
        if (message == WM_NCDESTROY)
        {
            self.m_CaptionCaptured = false;
            RemoveWindowSubclass(window, Procedure, reinterpret_cast<UINT_PTR>(&self));
            self.m_Window = nullptr;
            return DefSubclassProc(window, message, wParam, lParam);
        }
        if (!owner || !self.m_Enabled)
        {
            if (!owner)
            {
                self.ReleaseCaption();
            }
            return DefSubclassProc(window, message, wParam, lParam);
        }
        try
        {
            switch (message)
            {
            case WM_NCCALCSIZE:
                self.CalculateClient(wParam, lParam);
                return 0;
            case WM_NCACTIVATE:
                // Maintain activation without repainting a standard caption over the GPU.
                return DefSubclassProc(window, message, wParam, -1);
            case 0x00AE: // WM_NCUAHDRAWCAPTION: themed caption paint, also handled by Chromium.
            case 0x00AF: // WM_NCUAHDRAWFRAME.
                return 0;
            case WM_NCPAINT:
                return 0;
            case WM_ACTIVATE:
                self.Extend();
                if (LOWORD(wParam) == WA_INACTIVE)
                {
                    self.CancelCaption();
                }
                break;
            case WM_DWMCOMPOSITIONCHANGED:
            case WM_THEMECHANGED:
                self.Extend();
                SetWindowPos(window, nullptr, 0, 0, 0, 0,
                             SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                break;
            case WM_NCHITTEST:
                return self.HitTest(lParam);
            case WM_NCMOUSEMOVE:
                self.Pointer(self.ClientPoint(lParam, true), PointerEventType::Move, self.m_CaptionCaptured);
                if (self.m_Window)
                {
                    TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, window, 0};
                    TrackMouseEvent(&tracking);
                }
                return 0;
            case WM_NCMOUSELEAVE:
                if (!self.m_CaptionCaptured)
                {
                    self.Pointer({-100000, -100000}, PointerEventType::Move, false);
                }
                return 0;
            case WM_NCLBUTTONDOWN:
            case WM_NCLBUTTONDBLCLK:
                if (IsCaptionButton(wParam))
                {
                    const auto position = self.ClientPoint(lParam, true);
                    SetCapture(window);
                    self.m_CaptionCaptured = GetCapture() == window;
                    if (self.m_CaptionCaptured)
                    {
                        self.Pointer(position, PointerEventType::Down, true);
                    }
                    return 0;
                }
                break;
            case WM_MOUSEMOVE:
                if (self.m_CaptionCaptured)
                {
                    self.Pointer(self.ClientPoint(lParam, false), PointerEventType::Move, true);
                    return 0;
                }
                break;
            case WM_LBUTTONUP:
            case WM_NCLBUTTONUP:
                if (self.m_CaptionCaptured)
                {
                    const auto position = self.ClientPoint(lParam, message == WM_NCLBUTTONUP);
                    self.ReleaseCaption();
                    self.Pointer(position, PointerEventType::Up, false);
                    return 0;
                }
                if (message == WM_NCLBUTTONUP && IsCaptionButton(wParam))
                {
                    return 0;
                }
                break;
            case WM_CAPTURECHANGED:
                if (reinterpret_cast<HWND>(lParam) != window)
                {
                    self.CancelCaption();
                }
                break;
            case WM_CANCELMODE:
                self.CancelCaption();
                break;
            }
        }
        catch (...)
        {
            self.m_Error = std::current_exception();
            self.ReleaseCaption();
            return message == WM_NCHITTEST ? HTCLIENT : 0;
        }
        return self.m_Window ? DefSubclassProc(window, message, wParam, lParam) : 0;
    }
#endif
};

NativeWindowChrome::NativeWindowChrome(void* nativeWindow, std::weak_ptr<void> owner,
                                       std::function<WindowHitRegion(Point)> hitTest,
                                       std::function<void(PointerEvent)> pointer)
    : m_Impl(std::make_shared<Impl>())
{
    m_Impl->m_Owner = std::move(owner);
    m_Impl->m_HitTest = std::move(hitTest);
    m_Impl->m_Pointer = std::move(pointer);
#ifdef _WIN32
    m_Impl->m_Window = static_cast<HWND>(nativeWindow);
    if (!m_Impl->m_Window ||
        !SetWindowSubclass(m_Impl->m_Window, Impl::Procedure, reinterpret_cast<UINT_PTR>(m_Impl.get()),
                           reinterpret_cast<DWORD_PTR>(m_Impl.get())))
    {
        throw std::runtime_error("Could not install native title-bar integration.");
    }
#else
    (void)nativeWindow;
#endif
}

NativeWindowChrome::~NativeWindowChrome()
{
#ifdef _WIN32
    const auto state = m_Impl;
    if (state->m_Window)
    {
        try
        {
            state->CancelCaption();
        }
        catch (...)
        {
            state->m_Error = std::current_exception();
        }
        if (state->m_Window)
        {
            RemoveWindowSubclass(state->m_Window, Impl::Procedure, reinterpret_cast<UINT_PTR>(state.get()));
            state->m_Window = nullptr;
        }
    }
#endif
}

void NativeWindowChrome::SetEnabled(bool enabled)
{
    const auto state = m_Impl;
    if (state->m_Enabled == enabled)
    {
        return;
    }
    state->m_Enabled = enabled;
#ifdef _WIN32
    if (state->m_Window)
    {
        RECT client{};
        GetClientRect(state->m_Window, &client);
        const bool restored = !IsZoomed(state->m_Window) && !IsIconic(state->m_Window);
        if (!enabled)
        {
            state->CancelCaption();
        }
        if (state->m_Window)
        {
            state->Extend();
            SetWindowPos(state->m_Window, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (restored && state->m_Window)
            {
                RECT outer{}, changed{};
                GetWindowRect(state->m_Window, &outer);
                GetClientRect(state->m_Window, &changed);
                SetWindowPos(state->m_Window, nullptr, 0, 0, outer.right - outer.left + client.right - changed.right,
                             outer.bottom - outer.top + client.bottom - changed.bottom,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }
#endif
}

bool NativeWindowChrome::ResizeClient(int width, int height)
{
#ifdef _WIN32
    const auto state = m_Impl;
    if (state->m_Enabled && state->m_Window && !IsZoomed(state->m_Window) && !IsIconic(state->m_Window))
    {
        RECT outer{}, client{};
        GetWindowRect(state->m_Window, &outer);
        GetClientRect(state->m_Window, &client);
        const auto dimension = [](int value, LONG frame)
        {
            return static_cast<int>(std::clamp(static_cast<long long>(value) + frame, 1LL,
                                               static_cast<long long>(std::numeric_limits<int>::max())));
        };
        if (!SetWindowPos(state->m_Window, nullptr, 0, 0, dimension(width, outer.right - outer.left - client.right),
                          dimension(height, outer.bottom - outer.top - client.bottom),
                          SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
        {
            throw std::runtime_error("Could not resize the custom window client area.");
        }
        return true;
    }
#else
    (void)width;
    (void)height;
#endif
    return false;
}

void NativeWindowChrome::RethrowError() const
{
    if (m_Impl->m_Error)
    {
        std::rethrow_exception(m_Impl->m_Error);
    }
}
} // namespace EStudioUI::Detail
