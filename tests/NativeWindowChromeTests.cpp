#include "NativeWindowChrome.h"
#include "NativeResizePump.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;
int s_DefaultCaptionPresses = 0;
int s_DefaultCaptionPaints = 0;
LPARAM s_ActivationPaint = 0;

LRESULT CALLBACK ProbeProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint{};
        const auto dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const auto brush = CreateSolidBrush(RGB(197, 23, 151));
        FillRect(dc, &client, brush);
        DeleteObject(brush);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_NCLBUTTONDOWN && (wParam == HTMINBUTTON || wParam == HTMAXBUTTON || wParam == HTCLOSE))
    {
        ++s_DefaultCaptionPresses;
        return 0; // A broken adapter must fail the assertion instead of entering User32's modal button tracker.
    }
    if (message == WM_NCACTIVATE)
    {
        s_ActivationPaint = lParam;
    }
    if (message == 0x00AE || message == 0x00AF)
    {
        ++s_DefaultCaptionPaints;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void Check(bool condition, const char* message)
{
    ++s_Checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

bool CustomCaptionPixels(HWND window)
{
    SetWindowPos(window, HWND_TOPMOST, 80, 80, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    for (int attempt = 0; attempt < 30; ++attempt)
    {
        RedrawWindow(window, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
        DwmFlush();
        RECT client{};
        GetClientRect(window, &client);
        HDC screen = GetDC(nullptr);
        bool correct = screen != nullptr;
        for (const LONG x : {80L, client.right / 2, client.right - 25})
        {
            POINT point{x, 12};
            ClientToScreen(window, &point);
            const auto color = GetPixel(screen, point.x, point.y);
            correct = correct && color != CLR_INVALID && abs(static_cast<int>(GetRValue(color)) - 197) <= 12 &&
                      abs(static_cast<int>(GetGValue(color)) - 23) <= 12 &&
                      abs(static_cast<int>(GetBValue(color)) - 151) <= 12;
        }
        if (screen)
        {
            ReleaseDC(nullptr, screen);
        }
        if (correct)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

LRESULT Hit(HWND window, int x, int y)
{
    POINT point{x, y};
    ClientToScreen(window, &point);
    return SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
}
} // namespace

int main()
{
    HWND window = nullptr;
    try
    {
        WNDCLASSW type{};
        type.lpfnWndProc = ProbeProcedure;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = L"EStudioUI.NativeWindowChrome.Probe";
        Check(RegisterClassW(&type) != 0, "Register native chrome probe class");
        window = CreateWindowW(type.lpszClassName, L"EStudioUI chrome probe", WS_OVERLAPPEDWINDOW, 80, 80, 720, 480,
                               nullptr, nullptr, type.hInstance, nullptr);
        Check(window != nullptr, "Create a real decorated Win32 window");
        ShowWindow(window, SW_SHOWNOACTIVATE);
        RECT original{};
        GetClientRect(window, &original);
        auto owner = std::make_shared<int>(0);
        int hoverEvents = 0;
        int hitQueries = 0;
        std::vector<PointerEvent> pointerEvents;
        Detail::NativeWindowChrome chrome(
            window, owner,
            [&](Point point)
            {
                ++hitQueries;
                if (point.m_Y >= 50)
                {
                    return WindowHitRegion::Client;
                }
                if (point.m_X < 80)
                {
                    return WindowHitRegion::Caption;
                }
                if (point.m_X < 180)
                {
                    return WindowHitRegion::Client;
                }
                if (point.m_X < 240)
                {
                    return WindowHitRegion::Minimize;
                }
                if (point.m_X < 300)
                {
                    return WindowHitRegion::Maximize;
                }
                if (point.m_X < 360)
                {
                    return WindowHitRegion::Close;
                }
                return WindowHitRegion::Caption;
            },
            [&](PointerEvent event)
            {
                pointerEvents.push_back(event);
                if (event.m_Type == PointerEventType::Move)
                {
                    ++hoverEvents;
                }
            });
        chrome.SetEnabled(true);
        RECT extended{};
        GetClientRect(window, &extended);
        Check(extended.bottom == original.bottom && extended.right == original.right,
              "Replacing the native caption preserves the requested client size");
        RECT simpleCalculation{80, 80, 800, 560};
        SendMessageW(window, WM_NCCALCSIZE, FALSE, reinterpret_cast<LPARAM>(&simpleCalculation));
        Check(simpleCalculation.top == 81, "Simple and extended NCCALCSIZE requests use the same custom top frame");
        NCCALCSIZE_PARAMS calculation{};
        calculation.rgrc[0] = {80, 80, 800, 560};
        Check(SendMessageW(window, WM_NCCALCSIZE, TRUE, reinterpret_cast<LPARAM>(&calculation)) == 0 &&
                  calculation.rgrc[0].top == 81,
              "Extended client calculation returns no stale native caption preservation flags");
        for (const LONG size : {0L, 1L, 4L, 16L})
        {
            RECT tinyClient{80, 80, 80 + size, 80 + size};
            SendMessageW(window, WM_NCCALCSIZE, FALSE, reinterpret_cast<LPARAM>(&tinyClient));
            Check(tinyClient.left >= 80 && tinyClient.right >= tinyClient.left && tinyClient.right <= 80 + size &&
                      tinyClient.top >= 80 && tinyClient.bottom >= tinyClient.top && tinyClient.bottom <= 80 + size,
                  "Simple client calculation stays nonnegative inside tiny and empty native frames");
            calculation.rgrc[0] = {80, 80, 80 + size, 80 + size};
            SendMessageW(window, WM_NCCALCSIZE, TRUE, reinterpret_cast<LPARAM>(&calculation));
            const auto& result = calculation.rgrc[0];
            Check(result.left >= 80 && result.right >= result.left && result.right <= 80 + size &&
                      result.top >= 80 && result.bottom >= result.top && result.bottom <= 80 + size,
                  "Extended client calculation stays nonnegative inside tiny and empty native frames");
        }
        POINT origin{};
        ClientToScreen(window, &origin);
        RECT frame{};
        GetWindowRect(window, &frame);
        Check(origin.y - frame.top == 1,
              "A restored custom window has one physical top border, not a second title bar");
        SendMessageW(window, WM_NCACTIVATE, FALSE, 0);
        Check(s_ActivationPaint == -1, "Activation bookkeeping cannot repaint the native caption over custom pixels");
        const int captionPaints = s_DefaultCaptionPaints;
        SendMessageW(window, 0x00AE, 0, 0);
        SendMessageW(window, 0x00AF, 0, 0);
        Check(s_DefaultCaptionPaints == captionPaints,
              "Themed caption redraw messages do not reach the default caption painter");
        Check(Hit(window, 40, 25) == HTCAPTION, "Blank title content delegates native move behavior");
        Check(Hit(window, 120, 25) == HTCLIENT, "An interactive title widget remains client input");
        Check(Hit(window, 210, 25) == HTMINBUTTON, "Prepared minimize geometry returns the native caption role");
        Check(Hit(window, 210, 1) == HTMINBUTTON,
              "The painted top of a caption control remains clickable above the ordinary resize band");
        Check(Hit(window, 270, 25) == HTMAXBUTTON,
              "Prepared maximize geometry exposes the Windows Snap Layout hit-test contract");
        Check(Hit(window, 330, 25) == HTCLOSE, "Prepared close geometry delegates native closure");
        Check(Hit(window, 270, 100) == HTCLIENT, "Ordinary application content remains client input");
        Check(Hit(window, extended.right - 24, 20) == HTCAPTION,
              "DWM's old close rectangle cannot override the prepared custom title geometry");
        RECT outer{};
        GetWindowRect(window, &outer);
        Check(SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(outer.left + 1, outer.top + 1)) == HTTOPLEFT,
              "Native corner resizing survives the extended title frame");
        Check(SendMessageW(window, WM_NCHITTEST, 0, MAKELPARAM(outer.right - 1, outer.bottom - 1)) == HTBOTTOMRIGHT,
              "Native bottom corner resizing remains available");
        const auto style = GetWindowLongPtrW(window, GWL_STYLE);
        Check((style & WS_THICKFRAME) && (style & WS_MAXIMIZEBOX) && (style & WS_MINIMIZEBOX) && (style & WS_SYSMENU),
              "Resizing, snap, caption commands and system menu retain native window styles");
        SendMessageW(window, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        Check(IsZoomed(window), "The system maximize command follows native behavior");
        SendMessageW(window, WM_SYSCOMMAND, SC_RESTORE, 0);
        Check(!IsZoomed(window), "The system restore command returns from maximized state");
        RECT beforeMinimize{};
        GetClientRect(window, &beforeMinimize);
        SendMessageW(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        RECT minimized{};
        GetClientRect(window, &minimized);
        Check(IsIconic(window) && minimized.right >= minimized.left && minimized.bottom >= minimized.top,
              "A real minimized custom frame retains a valid nonnegative client rectangle");
        SendMessageW(window, WM_SYSCOMMAND, SC_RESTORE, 0);
        GetClientRect(window, &minimized);
        origin = {};
        ClientToScreen(window, &origin);
        GetWindowRect(window, &frame);
        std::cout << "Native restore geometry: initial=" << extended.right << 'x' << extended.bottom
                  << ", before minimize=" << beforeMinimize.right << 'x' << beforeMinimize.bottom
                  << ", restored=" << minimized.right << 'x' << minimized.bottom
                  << ", top inset=" << origin.y - frame.top << ", iconic=" << IsIconic(window)
                  << ", zoomed=" << IsZoomed(window) << ".\n";
        Check(!IsIconic(window) && minimized.right == extended.right && minimized.bottom == extended.bottom &&
                  origin.y - frame.top == 1,
              "Restoring from minimize preserves client dimensions and the single custom caption");
        Check(GetSystemMenu(window, FALSE) != nullptr, "The native system menu remains available");
        const int previousHover = hoverEvents;
        POINT hover{270, 25};
        ClientToScreen(window, &hover);
        SendMessageW(window, WM_NCMOUSEMOVE, HTMAXBUTTON, MAKELPARAM(hover.x, hover.y));
        SendMessageW(window, WM_NCMOUSELEAVE, 0, 0);
        Check(hoverEvents >= previousHover + 2,
              "Non-client caption hover enters and leaves the ordinary presentation path");
        pointerEvents.clear();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTMAXBUTTON, MAKELPARAM(hover.x, hover.y));
        Check(GetCapture() == window && pointerEvents.size() == 1 &&
                  pointerEvents.front().m_Type == PointerEventType::Down &&
                  pointerEvents.front().m_Button == PointerButton::Left && pointerEvents.front().m_Buttons == 1 &&
                  pointerEvents.front().m_PreserveFocus,
              "Pressing the painted caption captures the native mouse and delivers one owning pointer down");
        SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(270, 25));
        Check(GetCapture() != window && pointerEvents.size() == 2 &&
                  pointerEvents.back().m_Type == PointerEventType::Up &&
                  pointerEvents.back().m_Position == Point{270, 25},
              "Captured client release returns to the same logical caption geometry");
        Check(s_DefaultCaptionPresses == 0,
              "Custom caption presses never start User32's differently positioned button tracker");
        pointerEvents.clear();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTMAXBUTTON, MAKELPARAM(hover.x, hover.y));
        SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-20, -30));
        SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(-20, -30));
        Check(pointerEvents.size() == 3 && pointerEvents[1].m_Position == Point{-20, -30} &&
                  pointerEvents[1].m_Buttons == 1 && pointerEvents.back().m_Buttons == 0,
              "Dragging out preserves signed coordinates and held buttons through release");
        pointerEvents.clear();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTMAXBUTTON, MAKELPARAM(hover.x, hover.y));
        SendMessageW(window, WM_CANCELMODE, 0, 0);
        Check(GetCapture() != window && pointerEvents.size() == 2 &&
                  pointerEvents.back().m_Type == PointerEventType::Cancel,
              "Native cancellation clears capture and sends one canonical pointer cancellation");
        pointerEvents.clear();
        SendMessageW(window, WM_NCLBUTTONDOWN, HTMAXBUTTON, MAKELPARAM(hover.x, hover.y));
        SendMessageW(window, WM_CAPTURECHANGED, 0, 0);
        Check(GetCapture() != window && pointerEvents.size() == 2 &&
                  pointerEvents.back().m_Type == PointerEventType::Cancel,
              "Losing native capture cancels without activating a caption command");
        Check(chrome.ResizeClient(680, 420), "An enabled custom frame handles client-size requests");
        RECT resized{};
        GetClientRect(window, &resized);
        Check(resized.right == 680 && resized.bottom == 420,
              "Custom client sizing does not add the removed native caption height");
        int ticks = 0;
        bool inside = false;
        Detail::NativeResizePump resize(
            [&]
            {
                if (inside && ++ticks >= 5)
                {
                    PostMessageW(window, WM_CANCELMODE, 0, 0);
                }
                return .016;
            });
        resize.Attach(window);
        std::jthread watchdog(
            [&](std::stop_token stop)
            {
                for (int index = 0; index < 300 && !stop.stop_requested(); ++index)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                if (!stop.stop_requested())
                {
                    PostMessageW(window, WM_CANCELMODE, 0, 0);
                }
            });
        inside = true;
        SendMessageW(window, WM_SYSCOMMAND, SC_SIZE | WMSZ_BOTTOMRIGHT, MAKELPARAM(800, 560));
        inside = false;
        watchdog.request_stop();
        Check(ticks >= 5, "Custom chrome preserves ordinary scheduler frames during the real SC_SIZE modal loop");
        resize.RethrowError();
        chrome.RethrowError();
        const auto captionStyle = GetWindowLongPtrW(window, GWL_STYLE);
        Check((captionStyle & (WS_THICKFRAME | WS_MAXIMIZEBOX | WS_SYSMENU)) ==
                  (WS_THICKFRAME | WS_MAXIMIZEBOX | WS_SYSMENU),
              "Custom rendering retains native resize, maximize and system-menu styles");
        Check(CustomCaptionPixels(window),
              "Desktop-composited pixels contain the custom caption rather than a system overlay");
        SetWindowTextW(window, L"Creator dirty title update *");
        SendMessageW(window, WM_NCACTIVATE, TRUE, 0);
        Check(CustomCaptionPixels(window), "Changing the title cannot redraw system caption buttons over the client");
        SendMessageW(window, WM_NCACTIVATE, FALSE, 0);
        SendMessageW(window, WM_THEMECHANGED, 0, 0);
        Check(CustomCaptionPixels(window), "Inactive/theme-change desktop pixels retain exactly one custom caption");
        RECT beforeDisable{};
        GetClientRect(window, &beforeDisable);
        chrome.SetEnabled(false);
        RECT restored{};
        GetClientRect(window, &restored);
        Check(restored.bottom == beforeDisable.bottom, "Restoring the native title frame preserves the client size");
        Check(!chrome.ResizeClient(680, 420),
              "Ordinary frame sizing remains delegated to the existing platform adapter");
        std::unique_ptr<Detail::NativeWindowChrome> retiring;
        retiring = std::make_unique<Detail::NativeWindowChrome>(
            window, owner, [](Point) { return WindowHitRegion::Maximize; },
            [&](PointerEvent event)
            {
                if (event.m_Type == PointerEventType::Down)
                {
                    retiring.reset();
                }
            });
        retiring->SetEnabled(true);
        hover = {270, 25};
        ClientToScreen(window, &hover);
        SendMessageW(window, WM_NCLBUTTONDOWN, HTMAXBUTTON, MAKELPARAM(hover.x, hover.y));
        Check(!retiring && GetCapture() != window,
              "An adapter can retire during its own pointer callback without leaving capture");
        auto expiringOwner = std::make_shared<int>(0);
        int expiredDeliveries = 0;
        Detail::NativeWindowChrome expiring(
            window, expiringOwner, [](Point) { return WindowHitRegion::Maximize; },
            [&](PointerEvent) { ++expiredDeliveries; });
        expiring.SetEnabled(true);
        hover = {270, 25};
        ClientToScreen(window, &hover);
        SendMessageW(window, WM_NCLBUTTONDOWN, HTMAXBUTTON, MAKELPARAM(hover.x, hover.y));
        expiringOwner.reset();
        SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(270, 25));
        Check(GetCapture() != window && expiredDeliveries == 1,
              "Owner expiry retires native capture without delivering to an expired receiver");
        expiring.SetEnabled(false);
        Check(hitQueries >= 6, "Hit-test adapter ran without a nested UI frame or event pump");
        DestroyWindow(window);
        window = nullptr;
        std::cout << "Native title chrome: " << s_Checks << " checks passed; SC_SIZE ticks=" << ticks << ".\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        if (window)
        {
            DestroyWindow(window);
        }
        std::cerr << "Native title chrome: " << error.what() << '\n';
        return 1;
    }
}
