#pragma once

#include <filesystem>

namespace EStudioUI
{
//! Verifies numeric editing, validators, slider pixels, split commands, and retained idle frames.
void VerifyInputControls(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless);
} // namespace EStudioUI
