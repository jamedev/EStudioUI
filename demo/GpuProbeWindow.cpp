#include "GpuProbeWindow.h"

#include <EStudioUI/Window.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

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
} // namespace EStudioUI
