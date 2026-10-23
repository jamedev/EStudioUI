#include "NativeWindowChrome.h"

#include <exception>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#endif

namespace EStudioUI::Detail
{
struct NativeWindowChrome::Impl
{
    std::weak_ptr<void> m_Owner;
    std::function<WindowHitRegion(Point)> m_HitTest;
    std::function<void(Point)> m_Hover;
    std::exception_ptr m_Error;
    bool m_Enabled = false;
#ifdef _WIN32
    HWND m_Window = nullptr;

    void Extend()
    {
        const MARGINS margins{0, 0, m_Enabled ? 1 : 0, 0};
        DwmExtendFrameIntoClientArea(m_Window, &margins);
    }

    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
    {
        auto& self = *reinterpret_cast<Impl*>(data);
        const auto lease = self.m_Owner.lock();
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(window, Procedure, reinterpret_cast<UINT_PTR>(&self));
            self.m_Window = nullptr;
            return DefSubclassProc(window, message, wParam, lParam);
        }
        if (!lease || !self.m_Enabled)
            return DefSubclassProc(window, message, wParam, lParam);
        try
        {
            if (message == WM_NCCALCSIZE)
            {
                auto& rectangle =
                    wParam ? reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0] : *reinterpret_cast<RECT*>(lParam);
                const LONG top = rectangle.top;
                const auto result = DefSubclassProc(window, message, wParam, lParam);
                const UINT dpi = GetDpiForWindow(window);
                const int border =
                    GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                rectangle.top = top + (IsZoomed(window) ? border : 1);
                return result;
            }
            if (message == WM_DWMCOMPOSITIONCHANGED || message == WM_ACTIVATE)
                self.Extend();
            if (message == WM_NCHITTEST)
            {
                LRESULT native = 0;
                if (DwmDefWindowProc(window, message, wParam, lParam, &native) &&
                    (native == HTCLOSE || native == HTMAXBUTTON || native == HTMINBUTTON))
                    return native;
                POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                RECT bounds{};
                GetWindowRect(window, &bounds);
                const UINT dpi = GetDpiForWindow(window);
                const int bx =
                    GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                const int by =
                    GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                if (!IsZoomed(window))
                {
                    const bool left = screen.x < bounds.left + bx, right = screen.x >= bounds.right - bx;
                    const bool top = screen.y < bounds.top + by, bottom = screen.y >= bounds.bottom - by;
                    if (top)
                        return left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP;
                    if (bottom)
                        return left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM;
                    if (left || right)
                        return left ? HTLEFT : HTRIGHT;
                }
                ScreenToClient(window, &screen);
                switch (self.m_HitTest({static_cast<float>(screen.x), static_cast<float>(screen.y)}))
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
            }
            if (message == WM_NCMOUSEMOVE && (wParam == HTMINBUTTON || wParam == HTMAXBUTTON || wParam == HTCLOSE))
            {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(window, &point);
                self.m_Hover({static_cast<float>(point.x), static_cast<float>(point.y)});
                TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, window, 0};
                TrackMouseEvent(&tracking);
            }
            if (message == WM_NCMOUSELEAVE)
                self.m_Hover({-100000, -100000});
        }
        catch (...)
        {
            self.m_Error = std::current_exception();
        }
        return DefSubclassProc(window, message, wParam, lParam);
    }
#endif
};

NativeWindowChrome::NativeWindowChrome(void* nativeWindow, std::weak_ptr<void> owner,
                                       std::function<WindowHitRegion(Point)> hitTest, std::function<void(Point)> hover)
    : m_Impl(std::make_unique<Impl>())
{
    m_Impl->m_Owner = std::move(owner);
    m_Impl->m_HitTest = std::move(hitTest);
    m_Impl->m_Hover = std::move(hover);
#ifdef _WIN32
    m_Impl->m_Window = static_cast<HWND>(nativeWindow);
    if (!m_Impl->m_Window ||
        !SetWindowSubclass(m_Impl->m_Window, Impl::Procedure, reinterpret_cast<UINT_PTR>(m_Impl.get()),
                           reinterpret_cast<DWORD_PTR>(m_Impl.get())))
        throw std::runtime_error("Could not install native title-bar integration.");
#else
    (void)nativeWindow;
#endif
}

NativeWindowChrome::~NativeWindowChrome()
{
#ifdef _WIN32
    if (m_Impl->m_Window)
        RemoveWindowSubclass(m_Impl->m_Window, Impl::Procedure, reinterpret_cast<UINT_PTR>(m_Impl.get()));
#endif
}

void NativeWindowChrome::SetEnabled(bool enabled)
{
    if (m_Impl->m_Enabled == enabled)
        return;
    m_Impl->m_Enabled = enabled;
#ifdef _WIN32
    if (m_Impl->m_Window)
    {
        m_Impl->Extend();
        SetWindowPos(m_Impl->m_Window, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
#endif
}

void NativeWindowChrome::RethrowError() const
{
    if (m_Impl->m_Error)
        std::rethrow_exception(m_Impl->m_Error);
}
} // namespace EStudioUI::Detail
