#pragma once

#include <filesystem>

namespace EStudioUI
{
//! Verifies curved canvas geometry, clipped text, group culling, and retained GPU output.
void VerifyCanvasCurves(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless);
} // namespace EStudioUI
