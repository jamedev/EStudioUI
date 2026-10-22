#pragma once

#include <filesystem>

namespace EStudioUI
{
//! Verifies rich widget parts, retained rendering, virtualization, and paint-only animation work.
void VerifyWidgetStyles(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless);
} // namespace EStudioUI
