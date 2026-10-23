#include <EStudioUI/Image.h>
#include "ImageDecoder.h"

#include <atomic>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace EStudioUI
{
namespace
{
std::atomic<std::uint64_t> s_NextImage{1};

void ValidateScale(float scale)
{
    if (!std::isfinite(scale) || scale < .125f || scale > 16)
        throw std::invalid_argument("Image pixel scale must be finite and in [0.125,16].");
}
} // namespace

struct Image::Data
{
    int m_Width = 0, m_Height = 0;
    float m_PixelScale = 1;
    std::uint64_t m_Identity = 0;
    std::vector<std::uint8_t> m_Pixels;
};

Image Image::FromRgba(int width, int height, std::span<const std::uint8_t> pixels, float pixelScale)
{
    ValidateScale(pixelScale);
    if (width < 1 || height < 1 || width > 16384 || height > 16384 ||
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) > 16 * 1024 * 1024)
        throw std::length_error("Images require positive dimensions, at most 16,384 per axis and 64 MiB RGBA8.");
    const auto bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (pixels.size() != bytes)
        throw std::invalid_argument("Image pixels must contain exactly width * height * 4 RGBA8 bytes.");
    auto data = std::make_shared<Data>();
    data->m_Width = width;
    data->m_Height = height;
    data->m_PixelScale = pixelScale;
    data->m_Pixels.assign(pixels.begin(), pixels.end());
    auto identity = s_NextImage.load(std::memory_order_relaxed);
    do
    {
        if (identity == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Image lifetime identities exhausted.");
    } while (!s_NextImage.compare_exchange_weak(identity, identity + 1, std::memory_order_relaxed));
    data->m_Identity = identity;
    Image image;
    image.m_Data = std::move(data);
    return image;
}

Image Image::Decode(std::span<const std::uint8_t> encoded, float pixelScale)
{
    ValidateScale(pixelScale);
    int width = 0, height = 0;
    const auto pixels = Detail::DecodeImage(encoded, width, height);
    return FromRgba(width, height, pixels, pixelScale);
}

Image Image::Load(const std::filesystem::path& path, float pixelScale)
{
    ValidateScale(pixelScale);
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        throw std::runtime_error("Cannot open image file.");
    const auto size = stream.tellg();
    if (size <= 0 || size > 32 * 1024 * 1024)
        throw std::length_error("Image files require 1 byte to 32 MiB.");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Cannot read the complete image file.");
    return Decode(bytes, pixelScale);
}

bool Image::IsEmpty() const noexcept
{
    return !m_Data;
}

int Image::GetWidth() const noexcept
{
    return m_Data ? m_Data->m_Width : 0;
}

int Image::GetHeight() const noexcept
{
    return m_Data ? m_Data->m_Height : 0;
}

float Image::GetPixelScale() const noexcept
{
    return m_Data ? m_Data->m_PixelScale : 1;
}

Point Image::GetSize() const noexcept
{
    return {static_cast<float>(GetWidth()) / GetPixelScale(), static_cast<float>(GetHeight()) / GetPixelScale()};
}

std::span<const std::uint8_t> Image::GetPixels() const noexcept
{
    return m_Data ? std::span<const std::uint8_t>(m_Data->m_Pixels) : std::span<const std::uint8_t>{};
}

std::uint64_t Image::GetIdentity() const noexcept
{
    return m_Data ? m_Data->m_Identity : 0;
}
} // namespace EStudioUI
