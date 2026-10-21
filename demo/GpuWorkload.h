#pragma once

#include <EStudioUI/TabWidget.h>
#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Checks retained pixels against full redraw, hidden animations, and bounded GPU work.
void VerifyGpuWorkload(Window& owner, const std::filesystem::path& assets, const std::filesystem::path& output);

//! Profiles the actual gallery tabs with full and retained redraw using asynchronous GPU timers.
void ProfileUiWorkload(Window& window, TabWidget& tabs, const std::filesystem::path& assets,
                       const std::filesystem::path& output);
} // namespace EStudioUI
