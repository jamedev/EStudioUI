#include <EStudioUI/BoxLayout.h>

#include "LayoutDetail.h"

namespace EStudioUI
{
BoxLayout::BoxLayout(Axis axis) : m_Axis(axis)
{
    if (axis != Axis::Horizontal && axis != Axis::Vertical)
        throw std::invalid_argument("Invalid box layout axis.");
}

std::string_view BoxLayout::GetName() const
{
    return m_Axis == Axis::Horizontal ? "Row" : "Column";
}

LayoutPlan BoxLayout::Compute(const LayoutContext& context) const
{
    using namespace Detail;
    LayoutPlan plan;
    const bool row = m_Axis == Axis::Horizontal;
    const auto count = context.m_Children.size();
    if (count == 0)
        return plan;
    std::vector<MeasureResult> measures;
    std::vector<float> widths(count);
    std::vector<bool> definiteWidths(count);
    std::vector<LayoutItem> horizontal;
    float horizontalMargins = 0;
    const float gaps = context.m_Gap * static_cast<float>(count - 1);
    for (std::size_t i = 0; i < count; ++i)
    {
        measures.push_back(MeasureChild(context, i));
        const auto& child = context.m_Children[i];
        horizontalMargins += HorizontalMargin(child);
        horizontal.push_back(Item(child, measures.back(), true, row));
    }
    if (row)
    {
        float natural = horizontalMargins + gaps;
        for (const auto& item : horizontal)
            natural += Basis(item);
        const auto allocation =
            Layout::Distribute(context.m_Constraints.m_Width.value_or(natural) - horizontalMargins - gaps, horizontal);
        widths = allocation.m_Sizes;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& child = context.m_Children[i];
            definiteWidths[i] =
                Definite(child.m_Width, context.m_DefiniteWidth &&
                                            (child.m_Width.m_Unit == Length::Unit::Fraction ||
                                             child.m_Width.m_Unit == Length::Unit::Percent || child.m_Grow > 0));
        }
    }
    else
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto& child = context.m_Children[i];
            const float available = std::max(
                0.0f, context.m_Constraints.m_Width.value_or(measures[i].m_Preferred.m_X + HorizontalMargin(child)) -
                          HorizontalMargin(child));
            widths[i] = CrossSize(child, measures[i], available, true, context.m_Alignment);
            definiteWidths[i] = Definite(
                child.m_Width,
                context.m_DefiniteWidth &&
                    (child.m_Width.m_Unit == Length::Unit::Fraction || child.m_Width.m_Unit == Length::Unit::Percent ||
                     (child.m_Width.m_Unit == Length::Unit::Auto && context.m_Alignment == LayoutAlignment::Stretch)));
        }
    for (std::size_t i = 0; i < count; ++i)
        if (measures[i].m_HeightForWidth)
            measures[i] = MeasureChild(context, i, {widths[i], {}}, definiteWidths[i]);

    std::vector<LayoutItem> mainItems;
    float mainMargins = 0, preferredMain = gaps, preferredCross = 0, minimumMain = gaps, minimumCross = 0;
    float ascent = 0, descent = 0;
    bool baseline = false;
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& child = context.m_Children[i];
        const auto& measured = measures[i];
        const float mainMargin = row ? HorizontalMargin(child) : VerticalMargin(child);
        const float crossMargin = row ? VerticalMargin(child) : HorizontalMargin(child);
        const auto item = Item(child, measured, row, true);
        mainItems.push_back(item);
        mainMargins += mainMargin;
        preferredMain += Basis(item) + mainMargin;
        minimumMain += item.m_Minimum + mainMargin;
        preferredCross = std::max(preferredCross, (row ? measured.m_Preferred.m_Y : widths[i]) + crossMargin);
        minimumCross = std::max(minimumCross, Minimum(child, measured, !row) + crossMargin);
        plan.m_Measure.m_HeightForWidth |= measured.m_HeightForWidth;
        if (row && context.m_Alignment == LayoutAlignment::Baseline && measured.m_Baseline)
        {
            baseline = true;
            ascent = std::max(ascent, *measured.m_Baseline + child.m_Margin.m_Top);
            descent = std::max(descent, measured.m_Preferred.m_Y - *measured.m_Baseline + child.m_Margin.m_Bottom);
        }
    }
    if (baseline)
        preferredCross = std::max(preferredCross, ascent + descent);
    plan.m_Measure.m_Preferred = row ? Point{preferredMain, preferredCross} : Point{preferredCross, preferredMain};
    plan.m_Measure.m_Minimum = row ? Point{minimumMain, minimumCross} : Point{minimumCross, minimumMain};
    const float mainAvailable =
        (row ? context.m_Constraints.m_Width : context.m_Constraints.m_Height).value_or(preferredMain);
    const float crossAvailable =
        (row ? context.m_Constraints.m_Height : context.m_Constraints.m_Width).value_or(preferredCross);
    const auto allocation = Layout::Distribute(mainAvailable - mainMargins - gaps, mainItems);
    float gap = context.m_Gap;
    if (context.m_Justification == LayoutJustification::SpaceBetween && count > 1)
        gap += allocation.m_Unused / static_cast<float>(count - 1);
    std::vector<float> crossSizes(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& child = context.m_Children[i];
        crossSizes[i] = row ? CrossSize(child, measures[i], std::max(0.0f, crossAvailable - VerticalMargin(child)),
                                        false, context.m_Alignment)
                            : widths[i];
        const float allocatedHeight = row ? crossSizes[i] : allocation.m_Sizes[i];
        if (measures[i].m_Baseline && (baseline || allocatedHeight != measures[i].m_Preferred.m_Y))
        {
            const bool definiteHeight =
                Definite(child.m_Height, context.m_DefiniteHeight && (child.m_Height.m_Unit == Length::Unit::Fraction ||
                                                                      child.m_Height.m_Unit == Length::Unit::Percent));
            measures[i] = MeasureChild(context, i, {widths[i], allocatedHeight}, definiteWidths[i], definiteHeight);
        }
    }
    if (baseline)
    {
        ascent = 0;
        for (std::size_t i = 0; i < count; ++i)
            if (measures[i].m_Baseline)
                ascent = std::max(ascent, *measures[i].m_Baseline + context.m_Children[i].m_Margin.m_Top);
        plan.m_Measure.m_Baseline = ascent;
    }
    float position = Offset(context.m_Justification, allocation.m_Unused);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& child = context.m_Children[i];
        const auto& margin = child.m_Margin;
        const float before = row ? margin.m_Left : margin.m_Top;
        const float after = row ? margin.m_Right : margin.m_Bottom;
        const float crossBefore = row ? margin.m_Top : margin.m_Left;
        const float crossAfter = row ? margin.m_Bottom : margin.m_Right;
        const float crossSpace = std::max(0.0f, crossAvailable - crossBefore - crossAfter);
        float crossPosition = crossBefore + CrossOffset(context.m_Alignment, crossSpace, crossSizes[i]);
        if (baseline && measures[i].m_Baseline)
            crossPosition = ascent - *measures[i].m_Baseline;
        position += before;
        const Rect bounds = row ? Rect{position, crossPosition, allocation.m_Sizes[i], crossSizes[i]}
                                : Rect{crossPosition, position, crossSizes[i], allocation.m_Sizes[i]};
        const bool definiteHeight =
            Definite(child.m_Height, context.m_DefiniteHeight && (child.m_Height.m_Unit == Length::Unit::Fraction ||
                                                                  child.m_Height.m_Unit == Length::Unit::Percent ||
                                                                  (!row && child.m_Grow > 0) ||
                                                                  (row && child.m_Height.m_Unit == Length::Unit::Auto &&
                                                                   context.m_Alignment == LayoutAlignment::Stretch)));
        plan.m_Slots.push_back({i, bounds, measures[i], definiteWidths[i], definiteHeight});
        if (!plan.m_Measure.m_Baseline && measures[i].m_Baseline)
            plan.m_Measure.m_Baseline = bounds.m_Y + *measures[i].m_Baseline;
        position += allocation.m_Sizes[i] + after + gap;
    }
    PercentProblems(context, plan);
    BoundsProblems(context, plan);
    return plan;
}
} // namespace EStudioUI
