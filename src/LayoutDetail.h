#pragma once

#include <EStudioUI/LayoutStrategy.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI::Detail
{
//! Returns the exterior horizontal spacing.
inline float HorizontalMargin(const LayoutChild& child)
{
    return child.m_Margin.m_Left + child.m_Margin.m_Right;
}

//! Returns the exterior vertical spacing.
inline float VerticalMargin(const LayoutChild& child)
{
    return child.m_Margin.m_Top + child.m_Margin.m_Bottom;
}

//! Merges measured minima beneath explicit upper limits.
inline float Minimum(const LayoutChild& child, const MeasureResult& measured, bool width)
{
    return std::min(width ? child.m_Maximum.m_X : child.m_Maximum.m_Y,
                    std::max(width ? child.m_Minimum.m_X : child.m_Minimum.m_Y,
                             width ? measured.m_Minimum.m_X : measured.m_Minimum.m_Y));
}

//! Applies the effective measured and explicit limits.
inline float ClampSize(const LayoutChild& child, const MeasureResult& measured, float size, bool width)
{
    return std::clamp(size, Minimum(child, measured, width), width ? child.m_Maximum.m_X : child.m_Maximum.m_Y);
}

//! Recognizes an independent dimension reference.
inline bool Definite(Length length, bool allocated)
{
    return length.m_Unit == Length::Unit::Pixels || allocated;
}

//! Converts widget sizing into the shared allocator input.
inline LayoutItem Item(const LayoutChild& child, const MeasureResult& measured, bool width, bool grow = false)
{
    const auto length = width ? child.m_Width : child.m_Height;
    const float minimum = Minimum(child, measured, width);
    return {length.m_Unit == Length::Unit::Fraction ? minimum
                                                    : (width ? measured.m_Preferred.m_X : measured.m_Preferred.m_Y),
            minimum, width ? child.m_Maximum.m_X : child.m_Maximum.m_Y,
            length.m_Unit == Length::Unit::Fraction ? length.m_Value : (grow ? child.m_Grow : 0)};
}

//! Returns the bounded starting size.
inline float Basis(const LayoutItem& item)
{
    return std::clamp(item.m_Basis, item.m_Minimum, item.m_Maximum);
}

//! Positions unused main-axis space.
inline float Offset(LayoutJustification justification, float unused)
{
    return justification == LayoutJustification::Center ? unused * 0.5f
           : justification == LayoutJustification::End  ? unused
                                                        : 0;
}

//! Applies cross-axis stretch beneath hard size limits.
inline float CrossSize(const LayoutChild& child, const MeasureResult& measured, float available, bool width,
                       LayoutAlignment alignment)
{
    const auto unit = (width ? child.m_Width : child.m_Height).m_Unit;
    const bool stretch =
        unit == Length::Unit::Fraction || (unit == Length::Unit::Auto && alignment == LayoutAlignment::Stretch);
    return ClampSize(child, measured,
                     stretch ? available : (width ? measured.m_Preferred.m_X : measured.m_Preferred.m_Y), width);
}

//! Positions a child inside its cross-axis margin budget.
inline float CrossOffset(LayoutAlignment alignment, float available, float size)
{
    return alignment == LayoutAlignment::Center ? (available - size) * 0.5f
           : alignment == LayoutAlignment::End  ? available - size
                                                : 0;
}

//! Records percentage rules without an independent parent reference.
inline void PercentProblems(const LayoutContext& context, LayoutPlan& plan)
{
    for (std::size_t i = 0; i < context.m_Children.size(); ++i)
    {
        const auto& child = context.m_Children[i];
        for (const bool width : {true, false})
            if ((width ? child.m_Width : child.m_Height).m_Unit == Length::Unit::Percent &&
                !(width ? context.m_DefiniteWidth : context.m_DefiniteHeight))
                plan.m_Problems.push_back(
                    {LayoutIssue::IndefinitePercentage, width ? Axis::Horizontal : Axis::Vertical, i, 0, 0,
                     "Percentage has no independent parent size; Content sizing is used to avoid a cycle."});
    }
}

//! Records children outside the available content rectangle.
inline void BoundsProblems(const LayoutContext& context, LayoutPlan& plan)
{
    for (const auto& slot : plan.m_Slots)
    {
        const auto& child = context.m_Children[slot.m_Index];
        for (const bool width : {true, false})
        {
            const auto available = width ? context.m_Constraints.m_Width : context.m_Constraints.m_Height;
            if (!available)
                continue;
            const float start = width ? slot.m_Bounds.m_X : slot.m_Bounds.m_Y;
            const float size = width ? slot.m_Bounds.m_Width : slot.m_Bounds.m_Height;
            const float after = width ? child.m_Margin.m_Right : child.m_Margin.m_Bottom;
            if (start < -0.01f || start + size + after > *available + 0.01f)
                plan.m_Problems.push_back(
                    {LayoutIssue::Overflow, width ? Axis::Horizontal : Axis::Vertical, slot.m_Index, *available,
                     std::max(size, start + size + after),
                     "Allocated child exceeds available space; fixed/content sizes and hard minima are preserved."});
        }
    }
}

//! Queries and validates one temporary child measurement.
inline MeasureResult MeasureChild(const LayoutContext& context, std::size_t index, MeasureConstraints constraints = {},
                                  bool definiteWidth = false, bool definiteHeight = false)
{
    auto result = context.m_Measure(index, constraints, definiteWidth, definiteHeight);
    result.Validate();
    return result;
}
} // namespace EStudioUI::Detail
