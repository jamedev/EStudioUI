#pragma once

#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Checks bounded glyph churn, admission failure and retained/full framebuffer equality.
void VerifyTextReadiness(Window& window, const std::filesystem::path& assets, const std::filesystem::path& output);

//! Measures deterministic paragraph workloads without synchronous pixel readback.
void ProfileTextReadiness(Window& window, const std::filesystem::path& output);

//! Builds a focused native input, typography and accessibility acceptance window.
void BuildTextReadiness(Window& window);
} // namespace EStudioUI
