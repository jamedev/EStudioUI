#pragma once

namespace EStudioUI
{
class Window;

//! Allows native framebuffer fixtures to exceed a small desktop without changing application window defaults.
void AllowOversizedGpuProbe(Window& window);
} // namespace EStudioUI
