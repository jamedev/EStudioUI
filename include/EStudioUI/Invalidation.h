#pragma once

#include "StyleProperty.h"

#include <cstdint>

namespace EStudioUI
{
//! Derived work required after a widget value changes.
enum class Invalidation : std::uint32_t
{
    //! Retains all derived work.
    None = 0,
    //! Rechecks selectors conservatively across the tree.
    Selector = 1u << 0,
    //! Refreshes presentation inherited by descendants.
    InheritedStyle = 1u << 1,
    //! Recomputes this widget's CSS and registered paint parts.
    Style = 1u << 2,
    //! Discards this widget's intrinsic and ancestor aggregate measurements.
    Measure = 1u << 3,
    //! Rearranges this widget and the ancestor allocation path.
    Arrange = 1u << 4,
    //! Refreshes transformed input geometry and stationary hover.
    HitTest = 1u << 5,
    //! Requests a frame while retaining style and geometry caches.
    Paint = 1u << 6,
    //! Rechecks tree-dependent selectors and layout after ownership changes.
    Structure = 1u << 7,
    //! Updates intrinsic content, allocation, input geometry, and painting.
    Content = (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6)
};

//! Combines independent invalidation impacts.
constexpr Invalidation operator|(Invalidation left, Invalidation right)
{
    return static_cast<Invalidation>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

//! Selects common invalidation impacts.
constexpr Invalidation operator&(Invalidation left, Invalidation right)
{
    return static_cast<Invalidation>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}

//! Appends impacts to an existing invalidation request.
constexpr Invalidation& operator|=(Invalidation& left, Invalidation right)
{
    return left = left | right;
}

//! Returns whether any requested impact is present.
constexpr bool HasInvalidation(Invalidation value, Invalidation flags)
{
    return (value & flags) != Invalidation::None;
}

//! Classifies computed properties using the shared paint-only metadata.
Invalidation GetStylePropertyInvalidation(StyleProperty property);
} // namespace EStudioUI
