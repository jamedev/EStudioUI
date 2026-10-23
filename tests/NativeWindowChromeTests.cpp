#include "NativeWindowChrome.h"
#include "NativeResizePump.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const char* message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
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
        type.lpfnWndProc = DefWindowProcW;
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
        Detail::NativeWindowChrome chrome(
            window, owner,
            [&](Point point)
            {
                ++hitQueries;
                if (point.m_Y >= 50)
                    return WindowHitRegion::Client;
                if (point.m_X < 80)
                    return WindowHitRegion::Caption;
                if (point.m_X < 180)
                    return WindowHitRegion::Client;
                if (point.m_X < 240)
                    return WindowHitRegion::Minimize;
                if (point.m_X < 300)
                    return WindowHitRegion::Maximize;
                if (point.m_X < 360)
                    return WindowHitRegion::Close;
                return WindowHitRegion::Caption;
            },
            [&](Point) { ++hoverEvents; });
        chrome.SetEnabled(true);
        RECT extended{};
        GetClientRect(window, &extended);
        Check(extended.bottom > original.bottom, "Extending the client frame makes native title-row space available");
        RECT simpleCalculation{80, 80, 800, 560};
        SendMessageW(window, WM_NCCALCSIZE, FALSE, reinterpret_cast<LPARAM>(&simpleCalculation));
        Check(simpleCalculation.top == 81, "Simple and extended NCCALCSIZE requests use the same custom top frame");
        Check(Hit(window, 40, 25) == HTCAPTION, "Blank title content delegates native move behavior");
        Check(Hit(window, 120, 25) == HTCLIENT, "An interactive title widget remains client input");
        Check(Hit(window, 210, 25) == HTMINBUTTON, "Prepared minimize geometry returns the native caption role");
        Check(Hit(window, 270, 25) == HTMAXBUTTON,
              "Prepared maximize geometry exposes the Windows Snap Layout hit-test contract");
        Check(Hit(window, 330, 25) == HTCLOSE, "Prepared close geometry delegates native closure");
        Check(Hit(window, 270, 100) == HTCLIENT, "Ordinary application content remains client input");
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
        Check(GetSystemMenu(window, FALSE) != nullptr, "The native system menu remains available");
        const int previousHover = hoverEvents;
        POINT hover{270, 25};
        ClientToScreen(window, &hover);
        SendMessageW(window, WM_NCMOUSEMOVE, HTMAXBUTTON, MAKELPARAM(hover.x, hover.y));
        SendMessageW(window, WM_NCMOUSELEAVE, 0, 0);
        Check(hoverEvents >= previousHover + 2,
              "Non-client caption hover enters and leaves the ordinary presentation path");
        int ticks = 0;
        bool inside = false;
        Detail::NativeResizePump resize(
            [&]
            {
                if (inside && ++ticks >= 5)
                    PostMessageW(window, WM_CANCELMODE, 0, 0);
                return .016;
            });
        resize.Attach(window);
        std::jthread watchdog(
            [&](std::stop_token stop)
            {
                for (int index = 0; index < 300 && !stop.stop_requested(); ++index)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (!stop.stop_requested())
                    PostMessageW(window, WM_CANCELMODE, 0, 0);
            });
        inside = true;
        SendMessageW(window, WM_SYSCOMMAND, SC_SIZE | WMSZ_BOTTOMRIGHT, MAKELPARAM(800, 560));
        inside = false;
        watchdog.request_stop();
        Check(ticks >= 5, "Custom chrome preserves ordinary scheduler frames during the real SC_SIZE modal loop");
        resize.RethrowError();
        chrome.RethrowError();
        RECT beforeDisable{};
        GetClientRect(window, &beforeDisable);
        chrome.SetEnabled(false);
        RECT restored{};
        GetClientRect(window, &restored);
        Check(restored.bottom < beforeDisable.bottom, "Disabling custom chrome restores the standard title frame");
        Check(hitQueries >= 6, "Hit-test adapter ran without a nested UI frame or event pump");
        DestroyWindow(window);
        window = nullptr;
        std::cout << "Native title chrome: " << s_Checks << " checks passed; SC_SIZE ticks=" << ticks << ".\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        if (window)
            DestroyWindow(window);
        std::cerr << "Native title chrome: " << error.what() << '\n';
        return 1;
    }
}
