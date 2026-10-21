#pragma once

#include <filesystem>

namespace EStudioUI
{
//! Checks analytic canvas primitives, item identities, clipping, and retained framebuffer parity.
void VerifyCanvasPrimitives(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless);
} // namespace EStudioUI
