#pragma once
#include <filesystem>

namespace EStudioUI
{
class DatabaseBrowserWindow;
class Window;
//! Exercises the actual C++ browser, worker I/O, GPU views, captured dragging and typed editors.
void VerifyDatabaseBrowser(DatabaseBrowserWindow& browser, Window& window, const std::filesystem::path& assets,
                           const std::filesystem::path& output);
} // namespace EStudioUI
