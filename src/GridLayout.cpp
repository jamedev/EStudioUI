#include <EStudioUI/GridLayout.h>

#include "LayoutDetail.h"

namespace EStudioUI
{
GridLayout::GridLayout(std::vector<SizeRule> columns, std::vector<SizeRule> rows)
    : m_Columns(std::move(columns)), m_Rows(std::move(rows))
{
    if (m_Columns.empty() || m_Columns.size() > 10000 || m_Rows.size() > 10000)
        throw std::invalid_argument("Grid requires 1..10000 columns and at most 10000 explicit rows.");
    for (const auto& rule : m_Columns)
        rule.Validate();
    for (const auto& rule : m_Rows)
        rule.Validate();
}

std::string_view GridLayout::GetName() const
{
    return "Grid";
}

LayoutPlan GridLayout::Compute(const LayoutContext& context) const
{
    using namespace Detail;
    LayoutPlan plan;
    const auto count = context.m_Children.size();
    const auto columns = m_Columns.size();
    const auto rows = std::max(m_Rows.size(), (count + columns - 1) / columns);
    if (rows == 0)
        return plan;
    std::vector<MeasureResult> measures;
    std::vector<float> preferredWidths(columns), minimumWidths(columns), preferredHeights(rows), minimumHeights(rows);
    for (std::size_t i = 0; i < count; ++i)
    {
        measures.push_back(MeasureChild(context, i));
        const auto& child = context.m_Children[i];
        const auto column = i % columns;
        preferredWidths[column] =
            std::max(preferredWidths[column], Basis(Item(child, measures.back(), true)) + HorizontalMargin(child));
        minimumWidths[column] =
            std::max(minimumWidths[column], Minimum(child, measures.back(), true) + HorizontalMargin(child));
    }
    const auto trackItem = [](SizeRule rule, float preferred, float minimum)
    {
        const float lower = rule.m_Mode == SizeRule::Mode::Fixed
                                ? rule.m_Minimum
                                : std::min(rule.m_Maximum, std::max(rule.m_Minimum, minimum));
        return LayoutItem{rule.m_Mode == SizeRule::Mode::Fixed  ? rule.m_Value
                          : rule.m_Mode == SizeRule::Mode::Fill ? lower
                                                                : preferred,
                          lower, rule.m_Maximum, rule.m_Mode == SizeRule::Mode::Fill ? rule.m_Value : 0};
    };
    std::vector<LayoutItem> columnItems;
    float naturalWidth = context.m_Gap * static_cast<float>(columns - 1), minimumWidth = naturalWidth;
    for (std::size_t column = 0; column < columns; ++column)
    {
        const auto item = trackItem(m_Columns[column], preferredWidths[column], minimumWidths[column]);
        columnItems.push_back(item);
        naturalWidth += Basis(item);
        minimumWidth += item.m_Minimum;
    }
    const auto columnAllocation = Layout::Distribute(context.m_Constraints.m_Width.value_or(naturalWidth) -
                                                         context.m_Gap * static_cast<float>(columns - 1),
                                                     columnItems);
    std::vector<float> widths(count);
    std::vector<bool> definiteWidths(count);
    std::vector<float> ascents(rows), descents(rows);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& child = context.m_Children[i];
        const auto column = i % columns;
        const auto row = i / columns;
        const auto mode = m_Columns[column].m_Mode;
        const float available = std::max(0.0f, columnAllocation.m_Sizes[column] - HorizontalMargin(child));
        widths[i] = CrossSize(child, measures[i], available, true, LayoutAlignment::Stretch);
        definiteWidths[i] = Definite(child.m_Width, mode == SizeRule::Mode::Fixed ||
                                                        (context.m_DefiniteWidth && mode == SizeRule::Mode::Fill));
        if (measures[i].m_HeightForWidth)
            measures[i] = MeasureChild(context, i, {widths[i], {}}, definiteWidths[i]);
        preferredHeights[row] =
            std::max(preferredHeights[row], Basis(Item(child, measures[i], false)) + VerticalMargin(child));
        minimumHeights[row] = std::max(minimumHeights[row], Minimum(child, measures[i], false) + VerticalMargin(child));
        if (context.m_Alignment == LayoutAlignment::Baseline && measures[i].m_Baseline)
        {
            ascents[row] = std::max(ascents[row], *measures[i].m_Baseline + child.m_Margin.m_Top);
            descents[row] = std::max(descents[row],
                                     measures[i].m_Preferred.m_Y - *measures[i].m_Baseline + child.m_Margin.m_Bottom);
            preferredHeights[row] = std::max(preferredHeights[row], ascents[row] + descents[row]);
        }
    }
    std::vector<LayoutItem> rowItems;
    float naturalHeight = context.m_Gap * static_cast<float>(rows - 1), minimumHeight = naturalHeight;
    for (std::size_t row = 0; row < rows; ++row)
    {
        const auto item = trackItem(row < m_Rows.size() ? m_Rows[row] : SizeRule::Content(), preferredHeights[row],
                                    minimumHeights[row]);
        rowItems.push_back(item);
        naturalHeight += Basis(item);
        minimumHeight += item.m_Minimum;
    }
    const auto rowAllocation = Layout::Distribute(context.m_Constraints.m_Height.value_or(naturalHeight) -
                                                      context.m_Gap * static_cast<float>(rows - 1),
                                                  rowItems);
    for (std::size_t column = 0; column < columns; ++column)
        plan.m_Tracks.push_back({Axis::Horizontal, column, m_Columns[column], preferredWidths[column],
                                 columnItems[column].m_Minimum, columnAllocation.m_Sizes[column]});
    for (std::size_t row = 0; row < rows; ++row)
        plan.m_Tracks.push_back({Axis::Vertical, row, row < m_Rows.size() ? m_Rows[row] : SizeRule::Content(),
                                 preferredHeights[row], rowItems[row].m_Minimum, rowAllocation.m_Sizes[row]});
    std::vector<float> columnPositions(columns), rowPositions(rows);
    float x = Offset(context.m_Justification, columnAllocation.m_Unused);
    float horizontalGap = context.m_Gap;
    if (context.m_Justification == LayoutJustification::SpaceBetween && columns > 1)
        horizontalGap += columnAllocation.m_Unused / static_cast<float>(columns - 1);
    for (std::size_t column = 0; column < columns; ++column)
    {
        columnPositions[column] = x;
        x += columnAllocation.m_Sizes[column] + horizontalGap;
    }
    float y = 0;
    for (std::size_t row = 0; row < rows; ++row)
    {
        rowPositions[row] = y;
        y += rowAllocation.m_Sizes[row] + context.m_Gap;
    }
    std::vector<float> heights(count);
    std::vector<bool> definiteHeights(count);
    std::fill(ascents.begin(), ascents.end(), 0);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& child = context.m_Children[i];
        const auto row = i / columns;
        const auto rule = row < m_Rows.size() ? m_Rows[row] : SizeRule::Content();
        heights[i] = CrossSize(child, measures[i], std::max(0.0f, rowAllocation.m_Sizes[row] - VerticalMargin(child)),
                               false, context.m_Alignment);
        definiteHeights[i] =
            Definite(child.m_Height, rule.m_Mode == SizeRule::Mode::Fixed ||
                                         (context.m_DefiniteHeight && rule.m_Mode == SizeRule::Mode::Fill));
        if (measures[i].m_Baseline &&
            (context.m_Alignment == LayoutAlignment::Baseline || heights[i] != measures[i].m_Preferred.m_Y))
        {
            measures[i] = MeasureChild(context, i, {widths[i], heights[i]}, definiteWidths[i], definiteHeights[i]);
            ascents[row] = std::max(ascents[row], measures[i].m_Baseline.value_or(0) + child.m_Margin.m_Top);
        }
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& child = context.m_Children[i];
        const auto column = i % columns;
        const auto row = i / columns;
        float top = child.m_Margin.m_Top +
                    CrossOffset(context.m_Alignment, rowAllocation.m_Sizes[row] - VerticalMargin(child), heights[i]);
        if (context.m_Alignment == LayoutAlignment::Baseline && measures[i].m_Baseline)
            top = ascents[row] - *measures[i].m_Baseline;
        const Rect bounds{columnPositions[column] + child.m_Margin.m_Left, rowPositions[row] + top, widths[i],
                          heights[i]};
        plan.m_Slots.push_back({i, bounds, measures[i], definiteWidths[i], definiteHeights[i]});
        if (!plan.m_Measure.m_Baseline && measures[i].m_Baseline)
            plan.m_Measure.m_Baseline = bounds.m_Y + *measures[i].m_Baseline;
    }
    plan.m_Measure.m_Preferred = {naturalWidth, naturalHeight};
    plan.m_Measure.m_Minimum = {minimumWidth, minimumHeight};
    plan.m_Measure.m_HeightForWidth = count > 0;
    PercentProblems(context, plan);
    BoundsProblems(context, plan);
    return plan;
}
} // namespace EStudioUI
