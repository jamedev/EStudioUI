#pragma once

#include "Image.h"

#include <initializer_list>
#include <vector>

namespace EStudioUI
{
//! A bounded immutable set of raster densities sharing one natural logical size.
class Icon
{
  public:
    //! Creates an empty icon value.
    Icon() = default;

    //! Promotes one shared image to an icon without copying its pixels.
    Icon(Image image);

    //! Validates up to sixteen distinct densities with matching natural logical dimensions.
    Icon(std::initializer_list<Image> images);

    //! Returns whether no density contains image pixels.
    bool IsEmpty() const noexcept;

    //! Returns natural logical dimensions, or zero for an empty icon.
    Point GetSize() const noexcept;

    //! Chooses the smallest adequate density, falling back to the largest available.
    const Image& Select(float pixelScale) const;

    //! Compares shared image identities in density order.
    bool operator==(const Icon&) const noexcept = default;

  private:
    //! Retains at most sixteen immutable CPU images in increasing density order.
    std::vector<Image> m_Images;
};
} // namespace EStudioUI
