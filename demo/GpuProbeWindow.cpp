#include "GpuProbeWindow.h"

#include <EStudioUI/Window.h>
#include <EStudioUI/MainWindow.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/SemanticTree.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <dwmapi.h>
#endif

#include <stdexcept>
#include <cmath>
#include <fstream>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>

namespace EStudioUI
{
void AllowOversizedGpuProbe(Window& window)
{
    if (window.IsHeadless() || glfwGetPlatform() != GLFW_PLATFORM_WIN32)
    {
        return;
    }
    // Win32 otherwise constrains decorated fixtures to the desktop's default maximum track size.
    // Keep exact resize/DPI assertions even on a 1024x768 hosted desktop.
    auto* native = reinterpret_cast<GLFWwindow*>(window.GetContextIdentity());
    glfwSetWindowSizeLimits(native, GLFW_DONT_CARE, GLFW_DONT_CARE, 8192, 8192);
}

void PositionGpuProbeCursor(Window& window, Point position)
{
    auto* native = reinterpret_cast<GLFWwindow*>(window.GetContextIdentity());
    const auto scale = window.Platform().GetWindowMetrics().m_InputScale;
#ifdef _WIN32
    if (!window.IsHeadless() && glfwGetPlatform() == GLFW_PLATFORM_WIN32)
    {
        // GLFW silently ignores cursor positioning when Windows declines foreground activation.
        // The drop ownership probe needs the real cursor, but does not require keyboard focus.
        POINT screen{static_cast<LONG>(position.m_X * scale.m_X), static_cast<LONG>(position.m_Y * scale.m_Y)};
        if (!ClientToScreen(glfwGetWin32Window(native), &screen) || !SetCursorPos(screen.x, screen.y))
        {
            throw std::runtime_error("Cannot position the native framebuffer probe cursor.");
        }
        return;
    }
#endif
    glfwSetCursorPos(native, static_cast<double>(position.m_X) * scale.m_X,
                     static_cast<double>(position.m_Y) * scale.m_Y);
}

int VerifyGpuProbeChrome(Window& owner, const std::filesystem::path& assets, const std::filesystem::path& output)
{
#ifdef _WIN32
    if (owner.IsHeadless() || glfwGetPlatform() != GLFW_PLATFORM_WIN32)
    {
        return 0;
    }
    int checks = 0;
    const auto check = [&](bool passed, const char* message)
    {
        if (!passed)
        {
            throw std::runtime_error(std::string("Native title-bar GPU probe: ") + message);
        }
        ++checks;
    };
    const auto nativeHandle = [](Window& window)
    { return glfwGetWin32Window(reinterpret_cast<GLFWwindow*>(window.GetContextIdentity())); };
    const auto checkFrame = [&](Window& window)
    {
        RECT frame{};
        GetWindowRect(nativeHandle(window), &frame);
        POINT client{};
        ClientToScreen(nativeHandle(window), &client);
        check(client.y - frame.top == 1,
              "The actual GLFW client begins below one top-border pixel, not a native caption");
    };
    checkFrame(owner);
    BOOL nativeCaption = TRUE;
    check(SUCCEEDED(DwmGetWindowAttribute(nativeHandle(owner), DWMWA_NCRENDERING_ENABLED, &nativeCaption,
                                          sizeof(nativeCaption))) &&
              !nativeCaption,
          "The actual GLFW surface disables the system caption renderer, independently of client geometry");
    WindowOptions options;
    options.m_Width = 720;
    options.m_Height = 440;
    options.m_Title = "Title-bar probe";
    auto window = owner.CreateOwnedWindow(options);
    auto& shell = window->Ui().Root().Add<MainWindow>(*window);
    shell.TitleBar().Menus().AddMenu("File").AddAction("Example");
    auto& search = shell.TitleBar().Content().Add<TextBox>();
    search.SetWidth(SizeRule::Fixed(120));
    search.SetPlaceholder("Title search");
    shell.Central().Add<Label>("One title bar. Native frame commands. Actual GLFW input boundary.");
    const auto read = [](const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            throw std::runtime_error("Cannot read native title-bar probe theme: " + path.string());
        }
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    std::string error;
    check(window->Ui().SetStyleSheet(read(assets / "dark.css") + '\n' + read(assets / "essential-widgets.css"), error),
          "The native caption probe uses the production theme");
    window->SetPosition({32, 32});
    const auto render = [&] { window->RenderFrame(); };
    render();
    checkFrame(*window);
    window->SetSize(720, 440);
    render();
    const auto logicalSize = window->Platform().GetWindowMetrics().m_LogicalSize;
    check(std::abs(logicalSize.m_X - 720) < 1 && std::abs(logicalSize.m_Y - 440) < 1,
          "Logical SetSize keeps its exact contract with a custom native caption");
    const auto native = nativeHandle(*window);
    std::jthread watchdog(
        [native](std::stop_token stop)
        {
            std::mutex mutex;
            std::condition_variable_any changed;
            std::unique_lock lock(mutex);
            changed.wait_for(lock, stop, std::chrono::seconds(20), [] { return false; });
            if (!stop.stop_requested())
            {
                PostMessageW(native, WM_CANCELMODE, 0, 0);
            }
        });
    const auto center = [](Rect bounds)
    { return Point{bounds.m_X + bounds.m_Width / 2, bounds.m_Y + bounds.m_Height / 2}; };
    const auto nativePoint = [&](Point logical, bool screen)
    {
        const auto scale = window->Platform().GetWindowMetrics().m_InputScale;
        POINT point{static_cast<LONG>(std::lround(logical.m_X * scale.m_X)),
                    static_cast<LONG>(std::lround(logical.m_Y * scale.m_Y))};
        if (screen)
        {
            ClientToScreen(native, &point);
        }
        return MAKELPARAM(point.x, point.y);
    };
    const auto button = [&](WindowHitRegion role) -> Widget&
    {
        for (const auto& child : shell.TitleBar().GetChildren())
        {
            if (shell.TitleBar().HitTest(center(child->GetBounds())) == role)
            {
                return *child;
            }
        }
        throw std::runtime_error("Native title-bar GPU probe: caption control is missing.");
    };
    const auto click = [&](WindowHitRegion role, WPARAM hit)
    {
        const auto position = center(button(role).GetBounds());
        check(SendMessageW(native, WM_NCHITTEST, 0, nativePoint(position, true)) == static_cast<LRESULT>(hit),
              "Native hit testing uses the exact painted caption rectangle");
        SendMessageW(native, WM_NCLBUTTONDOWN, hit, nativePoint(position, true));
        SendMessageW(native, WM_LBUTTONUP, 0, nativePoint(position, false));
        render();
    };
    check(SendMessageW(native, WM_NCHITTEST, 0, nativePoint(center(search.GetBounds()), true)) == HTCLIENT,
          "An embedded title-bar editor remains client input");
    const auto caption = center(shell.TitleBar().Content().GetChildren().front()->GetBounds());
    check(SendMessageW(native, WM_NCHITTEST, 0, nativePoint(caption, true)) == HTCAPTION,
          "The painted title text retains native move and double-click semantics");
    const auto menuViewport = shell.TitleBar().GetChildren().front()->GetBounds();
    const Point blankMenu{menuViewport.m_X + menuViewport.m_Width - 2, menuViewport.m_Y + menuViewport.m_Height / 2};
    check(SendMessageW(native, WM_NCHITTEST, 0, nativePoint(blankMenu, true)) == HTCAPTION,
          "Unused title-menu space retains native window dragging");
    std::filesystem::create_directories(output);
    window->Graphics().SavePpm(output / "native-titlebar-restored.ppm");
    window->Ui().SetFocus(&search);
    auto& close = button(WindowHitRegion::Close);
    const auto closePoint = center(close.GetBounds());
    SendMessageW(native, WM_NCMOUSEMOVE, HTCLOSE, nativePoint(closePoint, true));
    render();
    check(close.IsHovered(), "Non-client hover reaches the GPU button");
    SendMessageW(native, WM_NCLBUTTONDOWN, HTCLOSE, nativePoint(closePoint, true));
    render();
    check(close.IsPressed() && GetCapture() == native, "The painted close button owns its press and native capture");
    check(window->Ui().GetFocus() == &search, "A non-client caption press preserves client editor keyboard focus");
    window->Graphics().SavePpm(output / "native-titlebar-pressed.ppm");
    SendMessageW(native, WM_MOUSEMOVE, MK_LBUTTON, nativePoint({20, 100}, false));
    SendMessageW(native, WM_LBUTTONUP, 0, nativePoint({20, 100}, false));
    render();
    check(!window->IsClosed() && !close.IsPressed() && GetCapture() != native,
          "Releasing outside cancels the close click without leaving pressed state");
    SendMessageW(native, WM_NCLBUTTONDOWN, HTCLOSE, nativePoint(closePoint, true));
    SendMessageW(native, WM_CANCELMODE, 0, 0);
    render();
    check(!window->IsClosed() && !close.IsPressed(), "Native cancellation does not activate close");
    click(WindowHitRegion::Minimize, HTMINBUTTON);
    check(IsIconic(native), "A single click on the painted minimize button minimizes the actual HWND");
    window->Platform().GetWindowMetrics().Validate();
    check(!window->IsClosed(), "Minimize retains a live window with valid native metrics");
    window->Restore();
    render();
    window->Ui().SetFocus(&search);
    click(WindowHitRegion::Maximize, HTMAXBUTTON);
    check(window->IsMaximized(), "A single click on the painted maximize button maximizes the actual HWND");
    render();
    check(window->Ui().GetFocus() == &search, "Maximizing retains the focused client editor");
    auto& semantics = window->Ui().Semantics();
    check(semantics.GetNode(semantics.GetId(button(WindowHitRegion::Maximize)))->m_Info.m_Name == "Restore window",
          "The maximized caption exposes a restore symbol and accessible command name");
    window->Graphics().SavePpm(output / "native-titlebar-maximized.ppm");
    click(WindowHitRegion::Maximize, HTMAXBUTTON);
    check(!window->IsMaximized(), "A single click on the same painted restore control restores the actual HWND");
    render();
    checkFrame(*window);
    click(WindowHitRegion::Close, HTCLOSE);
    check(window->IsClosed() && !owner.IsClosed(), "A single painted close click closes only its owning native window");
    std::cout << "Native GLFW title bar: " << checks << " checks passed\n";
    return checks;
#else
    (void)owner;
    (void)assets;
    (void)output;
    return 0;
#endif
}
} // namespace EStudioUI
