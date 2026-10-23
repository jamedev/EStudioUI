#include "ImageDecoder.h"

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace
{
// stb's internal allocations share a per-thread working budget, including inflated
// PNG data. Header dimensions alone cannot bound a malformed compressed stream.
constexpr std::size_t s_DecodeLimit = 128 * 1024 * 1024;
thread_local std::size_t s_DecodeBytes = 0;

struct alignas(std::max_align_t) Allocation
{
    std::size_t m_Bytes;
};

void* Allocate(std::size_t bytes)
{
    if (bytes > s_DecodeLimit - s_DecodeBytes)
        return nullptr;
    auto* header = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + bytes));
    if (!header)
        return nullptr;
    header->m_Bytes = bytes;
    s_DecodeBytes += bytes;
    return header + 1;
}

void Release(void* memory)
{
    if (!memory)
        return;
    auto* header = static_cast<Allocation*>(memory) - 1;
    s_DecodeBytes -= header->m_Bytes;
    std::free(header);
}

void* Resize(void* memory, std::size_t bytes)
{
    if (!memory)
        return Allocate(bytes);
    auto* header = static_cast<Allocation*>(memory) - 1;
    const auto previous = header->m_Bytes;
    if (bytes > s_DecodeLimit - (s_DecodeBytes - previous))
        return nullptr;
    auto* resized = static_cast<Allocation*>(std::realloc(header, sizeof(Allocation) + bytes));
    if (!resized)
        return nullptr;
    resized->m_Bytes = bytes;
    s_DecodeBytes = s_DecodeBytes - previous + bytes;
    return resized + 1;
}
} // namespace

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 16384
#define STBI_MALLOC(bytes) Allocate(bytes)
#define STBI_REALLOC(memory, bytes) Resize(memory, bytes)
#define STBI_FREE(memory) Release(memory)
#include <stb_image.h>

namespace EStudioUI::Detail
{
std::vector<std::uint8_t> DecodeImage(std::span<const std::uint8_t> encoded, int& width, int& height)
{
    if (encoded.empty() || encoded.size() > 32 * 1024 * 1024)
        throw std::length_error("Encoded images require 1 byte to 32 MiB.");
    const auto size = static_cast<int>(encoded.size());
    int channels = 0;
    if (!stbi_info_from_memory(encoded.data(), size, &width, &height, &channels))
        throw std::runtime_error("Cannot read image header; supported formats are PNG and JPEG.");
    if (width < 1 || height < 1 || width > 16384 || height > 16384 ||
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) > 16 * 1024 * 1024)
        throw std::length_error("Decoded images are limited to 16,384 pixels per axis and 64 MiB RGBA8.");
    const int expectedWidth = width, expectedHeight = height;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(encoded.data(), size, &width, &height, &channels, STBI_rgb_alpha), stbi_image_free);
    if (!pixels || width != expectedWidth || height != expectedHeight)
        throw std::runtime_error("Cannot decode image within the 128 MiB decoder working budget.");
    const auto bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    return {pixels.get(), pixels.get() + bytes};
}
} // namespace EStudioUI::Detail
