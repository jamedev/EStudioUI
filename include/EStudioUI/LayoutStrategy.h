#pragma once

#include "Layout.h"
#include "Measure.h"
#include "SizeRule.h"
#include "Style.h"

#include <cstdint>
#include <functional>
#include <string>

namespace EStudioUI
{
//! Cross-axis placement shared by built-in and custom layouts.
enum class LayoutAlignment
{
    Stretch,
    Start,
    Center,
    End,
    Baseline
};

//! Distribution of unused main-axis space.
enum class LayoutJustification
{
    Start,
    Center,
    End,
    SpaceBetween
};

//! An inspectable reason for allocation or content overflow.
enum class LayoutIssue
{
    Overflow,
    IndefinitePercentage,
    ContentClipped
};

//! Owned diagnostic data; no widget address or borrowed text is retained.
struct LayoutDiagnostic
{
    //! Diagnostic category.
    LayoutIssue m_Issue = LayoutIssue::Overflow;
    //! Axis of the affected dimension.
    Axis m_Axis = Axis::Horizontal;
    //! Container identity, stable for its lifetime.
    std::uint64_t m_Container = 0;
    //! Container's CSS identifier at the last layout.
    std::string m_ContainerId;
    //! Responsible child identity, or zero for the container as a whole.
    std::uint64_t m_Child = 0;
    //! Responsible child's CSS identifier at the last layout.
    std::string m_ChildId;
    //! Space available in logical pixels.
    float m_Available = 0;
    //! Space required in logical pixels.
    float m_Required = 0;
    //! Human-readable explanation of the preserved rule.
    std::string m_Message;
};

//! Pure child description without a widget, font, or platform dependency.
struct LayoutChild
{
    //! Effective outer width rule.
    Length m_Width;
    //! Effective outer height rule.
    Length m_Height;
    //! Hard lower bounds after measurement and explicit-rule precedence.
    Point m_Minimum;
    //! Hard upper bounds.
    Point m_Maximum{1000000, 1000000};
    //! Exterior spacing.
    Insets m_Margin;
    //! Legacy CSS growth weight on a box layout's main axis.
    float m_Grow = 0;
};

//! A child rectangle relative to the container's content origin.
struct LayoutSlot
{
    //! Index in the context's displayed, in-flow children.
    std::size_t m_Index = 0;
    //! Allocated outer rectangle in logical pixels.
    Rect m_Bounds;
    //! Measured requirements used for this allocation.
    MeasureResult m_Measure;
    //! Whether the width supplies a definite percentage reference to descendants.
    bool m_DefiniteWidth = false;
    //! Whether the height supplies a definite percentage reference to descendants.
    bool m_DefiniteHeight = false;
};

//! A strategy issue referring to an optional child index.
struct LayoutProblem
{
    //! Diagnostic category.
    LayoutIssue m_Issue = LayoutIssue::Overflow;
    //! Affected axis.
    Axis m_Axis = Axis::Horizontal;
    //! Responsible child, or no child for an aggregate allocation requirement.
    std::optional<std::size_t> m_Child;
    //! Available space.
    float m_Available = 0;
    //! Required space.
    float m_Required = 0;
    //! Explanation intended for application diagnostics.
    std::string m_Message;
};

//! One grid/form track's rule and final allocation.
struct LayoutTrack
{
    //! Horizontal columns or vertical rows.
    Axis m_Axis = Axis::Horizontal;
    //! Track index on its axis.
    std::size_t m_Index = 0;
    //! Explicit or implicit track rule.
    SizeRule m_Rule;
    //! Measured content demand before applying the track rule.
    float m_Preferred = 0;
    //! Effective hard lower bound.
    float m_Minimum = 0;
    //! Final logical-pixel size.
    float m_Allocated = 0;
};

//! Pure strategy input; callbacks are valid only during Compute and must not be retained.
struct LayoutContext
{
    //! Known content dimensions; an absent dimension requests natural size.
    MeasureConstraints m_Constraints;
    //! True when width comes from an independent allocation rather than content.
    bool m_DefiniteWidth = false;
    //! True when height comes from an independent allocation rather than content.
    bool m_DefiniteHeight = false;
    //! Spacing between children or tracks.
    float m_Gap = 0;
    //! Cross-axis alignment.
    LayoutAlignment m_Alignment = LayoutAlignment::Stretch;
    //! Distribution of unused main-axis space.
    LayoutJustification m_Justification = LayoutJustification::Start;
    //! Displayed, in-flow child descriptors in ownership order.
    std::vector<LayoutChild> m_Children;
    //! Measures one child's outer requirements at an optional allocated outer width/height.
    std::function<MeasureResult(std::size_t, MeasureConstraints, bool, bool)> m_Measure;
};

//! A finite, deterministic layout result reusable by diagnostics and arrangement.
struct LayoutPlan
{
    //! Aggregate content requirements before container border and padding.
    MeasureResult m_Measure;
    //! One allocation for each displayed, in-flow child.
    std::vector<LayoutSlot> m_Slots;
    //! Inspectable issues; hard sizing rules remain preserved.
    std::vector<LayoutProblem> m_Problems;
    //! Track decisions for strategies that allocate shared tracks.
    std::vector<LayoutTrack> m_Tracks;
};

//! An owned snapshot explaining one child's last allocation.
struct LayoutDecision
{
    //! Child identity, stable for its lifetime.
    std::uint64_t m_Child = 0;
    //! Child CSS identifier at the last allocation.
    std::string m_ChildId;
    //! Effective sizing and limits.
    LayoutChild m_Rules;
    //! Allocated rectangle and measured requirements.
    LayoutSlot m_Slot;
};

//! An owned snapshot of a container's last completed layout.
struct LayoutReport
{
    //! Strategy name.
    std::string m_Strategy;
    //! Container CSS identifier.
    std::string m_ContainerId;
    //! Allocated content dimensions.
    Point m_ContentSize;
    //! Requirements before final allocation.
    MeasureResult m_Measure;
    //! Actual content requirements before border/padding and outer size rules.
    MeasureResult m_ContentMeasure;
    //! Child decisions in ownership order.
    std::vector<LayoutDecision> m_Children;
    //! Overflow and unresolved sizing diagnostics.
    std::vector<LayoutDiagnostic> m_Diagnostics;
    //! Shared track rules and allocations.
    std::vector<LayoutTrack> m_Tracks;
    //! Formats rules, measurements, allocations, and causes for inspection.
    std::string Explain() const;
};

//! Extends container layout through pure measurement and allocation, without subclassing Widget.
class LayoutStrategy
{
  public:
    //! Releases an application-supplied strategy through its interface.
    virtual ~LayoutStrategy() = default;
    //! Returns a short diagnostic name.
    virtual std::string_view GetName() const = 0;
    //! Computes finite requirements and one slot per child; callbacks may not mutate UI state.
    virtual LayoutPlan Compute(const LayoutContext& context) const = 0;
};
} // namespace EStudioUI
