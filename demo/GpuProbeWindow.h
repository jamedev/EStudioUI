#pragma once

namespace EStudioUI
{
class Window;
struct Point;

//! Allows native framebuffer fixtures to exceed a small desktop without changing application window defaults.
void AllowOversizedGpuProbe(Window& window);

//! Positions the real probe cursor without requiring foreground activation on a hosted Windows desktop.
void PositionGpuProbeCursor(Window& window, Point position);
} // namespace EStudioUI
