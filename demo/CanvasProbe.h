#pragma once
#include <filesystem>

namespace EStudioUI
{
//! Checks actual canvas pixels, retained damage, and viewport-only submission at scale.
void VerifyCanvas(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless);
//! Writes reproducible scene-build, BVH-query, and completed-frame timings.
void ProfileCanvas(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless);
} // namespace EStudioUI
