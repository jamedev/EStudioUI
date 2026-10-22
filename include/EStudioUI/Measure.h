#pragma once

#include "Types.h"

#include <optional>

namespace EStudioUI
{
//! Allocated content dimensions; an absent dimension requests natural content size.
struct MeasureConstraints
{
    //! Available content width in logical pixels.
    std::optional<float> m_Width;

    //! Available content height in logical pixels.
    std::optional<float> m_Height;

    //! Rejects nonfinite or negative dimensions.
    void Validate() const;

    //! Compares the exact logical constraints used by the cache.
    bool operator==(const MeasureConstraints&) const = default;
};

//! Content requirements, before a widget's border, padding, and explicit size rules.
struct MeasureResult
{
    //! Smallest useful content size; explicit code limits retain precedence.
    Point m_Minimum;

    //! Natural content size under the supplied width constraint.
    Point m_Preferred;

    //! First baseline from the content top, including any vertical alignment offset.
    std::optional<float> m_Baseline;

    //! Requests another measurement when the allocated width changes.
    bool m_HeightForWidth = false;

    //! Rejects invalid extension results before they enter layout caches.
    void Validate() const;
};
} // namespace EStudioUI
