#include "GpuProbeWindow.h"

#include <EStudioUI/Window.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include <stdexcept>

namespace EStudioUI
{
void AllowOversizedGpuProbe(Window& window)
{
    if (window.IsHeadless() || glfwGetPlatform() != GLFW_PLATFORM_WIN32)
        return;
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
} // namespace EStudioUI
