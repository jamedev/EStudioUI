#pragma once

#include <filesystem>

namespace EStudioUI
{
//! Verifies scoped painter state, transforms, clips, custom CSS widgets, and retained framebuffer parity.
void VerifyPainter(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless);
} // namespace EStudioUI
