#pragma once

#include <filesystem>

namespace EStudioUI
{
//! Verifies native multiwindow scheduling, background wakeups, and deferred window lifetime.
void VerifyApplication(const std::filesystem::path& assets, const std::filesystem::path& output, bool headless);
} // namespace EStudioUI
