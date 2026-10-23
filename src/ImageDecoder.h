#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace EStudioUI::Detail
{
//! Decodes PNG/JPEG with bounded temporary allocation and returns top-down RGBA8 pixels.
std::vector<std::uint8_t> DecodeImage(std::span<const std::uint8_t> encoded, int& width, int& height);
} // namespace EStudioUI::Detail
