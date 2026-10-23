#pragma once

#include "Types.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace EStudioUI
{
//! Immutable, shared top-down RGBA8 pixels; creation and reads require no UI or GPU context.
class Image
{
  public:
    //! Creates an empty image value.
    Image() = default;

    //! Copies tightly packed straight-alpha RGBA8 pixels, with at most 64 MiB per image.
    static Image FromRgba(int width, int height, std::span<const std::uint8_t> pixels, float pixelScale = 1);

    //! Decodes a bounded PNG or JPEG memory buffer without accessing the UI.
    static Image Decode(std::span<const std::uint8_t> encoded, float pixelScale = 1);

    //! Reads and decodes a PNG or JPEG file through the native filesystem path.
    static Image Load(const std::filesystem::path& path, float pixelScale = 1);

    //! Returns whether this value contains no pixels.
    bool IsEmpty() const noexcept;

    //! Returns the physical pixel width, or zero for an empty image.
    int GetWidth() const noexcept;

    //! Returns the physical pixel height, or zero for an empty image.
    int GetHeight() const noexcept;

    //! Returns source pixels per logical pixel, independent of the destination monitor.
    float GetPixelScale() const noexcept;

    //! Returns natural logical dimensions using the source density.
    Point GetSize() const noexcept;

    //! Borrows straight-alpha RGBA8 bytes while this shared image value remains alive.
    std::span<const std::uint8_t> GetPixels() const noexcept;

    //! Returns a lifetime-unique resource identity, shared by copies; zero denotes empty.
    std::uint64_t GetIdentity() const noexcept;

    //! Compares immutable storage identities without scanning pixels.
    bool operator==(const Image&) const noexcept = default;

  private:
    friend class Renderer;

    //! Owns validated pixels, natural density, and a lifetime-unique identity.
    struct Data;

    //! Shares immutable CPU storage without retaining any renderer or native resource.
    std::shared_ptr<const Data> m_Data;
};
} // namespace EStudioUI
