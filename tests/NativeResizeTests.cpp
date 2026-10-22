#include "NativeResizePump.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
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
        type.lpszClassName = L"EStudioUIResizeProbe";
        Check(RegisterClassW(&type) != 0, "register native probe class");
        window = CreateWindowW(type.lpszClassName, L"EStudioUI live resize test", WS_OVERLAPPEDWINDOW, 40, 40, 360, 240,
                               nullptr, nullptr, type.hInstance, nullptr);
        Check(window != nullptr, "create native probe window without a GPU requirement");
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
        bool inside = false;
        int modalTicks = 0;
        int totalTicks = 0;
        int widths = 0;
        EStudioUI::Detail::NativeResizePump pump(
            [&]
            {
                ++totalTicks;
                if (inside)
                {
                    ++modalTicks;
                    if (modalTicks > 1)
                    {
                        SetWindowPos(window, nullptr, 0, 0, 360 + modalTicks * 3, 240,
                                     SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
                        ++widths;
                    }
                    if (modalTicks >= 8)
                        PostMessageW(window, WM_CANCELMODE, 0, 0);
                }
                return 0.016;
            });
        pump.Attach(window);
        // The watchdog prevents a regression from leaving CI trapped in DefWindowProc.
        std::atomic<bool> finished = false;
        std::jthread watchdog(
            [&](std::stop_token stop)
            {
                for (int i = 0; i < 300 && !stop.stop_requested(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if (!finished && !stop.stop_requested())
                    PostMessageW(window, WM_CANCELMODE, 0, 0);
            });
        inside = true;
        SendMessageW(window, WM_SYSCOMMAND, SC_SIZE | WMSZ_BOTTOMRIGHT, MAKELPARAM(390, 270));
        inside = false;
        finished = true;
        watchdog.request_stop();
        pump.RethrowError();
        Check(modalTicks >= 8 && widths >= 6,
              "scheduler must tick and change geometry before the native sizing call returns");
        const int completed = totalTicks;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(70);
        while (std::chrono::steady_clock::now() < deadline)
        {
            MSG event{};
            while (PeekMessageW(&event, nullptr, 0, 0, PM_REMOVE))
                DispatchMessageW(&event);
            std::this_thread::yield();
        }
        Check(totalTicks == completed, "resize timer is cancelled after leaving the native modal loop");
        DestroyWindow(window);
        window = nullptr;
        std::cout << "Win32 real SC_SIZE modal loop: " << modalTicks << " scheduler ticks, " << widths
                  << " intermediate sizes before SendMessage returned; timer stopped after exit.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        if (window)
            DestroyWindow(window);
        std::cerr << "Native resize: " << error.what() << '\n';
        return 1;
    }
}
