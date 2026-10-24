#pragma once
#include <filesystem>
#include <stop_token>

namespace EStudioUI
{
//! Creates an application-owned engineering sample once, preserving an existing file.
std::filesystem::path CreateExampleDatabase(const std::filesystem::path& path, std::size_t rows,
                                            std::stop_token stop = {});
} // namespace EStudioUI
