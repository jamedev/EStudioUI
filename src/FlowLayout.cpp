#include <EStudioUI/FlowLayout.h>
#include <EStudioUI/BoxLayout.h>

#include "LayoutDetail.h"

namespace EStudioUI
{
std::string_view FlowLayout::GetName() const
{
    return "Flow";
}

LayoutPlan FlowLayout::Compute(const LayoutContext& context) const
{
    using namespace Detail;
    LayoutPlan plan;
    std::vector<std::vector<std::size_t>> rows;
    std::vector<MeasureResult> natural;
    float used = 0;
    for (std::size_t i = 0; i < context.m_Children.size(); ++i)
    {
        auto measured = MeasureChild(context, i);
        const auto& child = context.m_Children[i];
        if (measured.m_HeightForWidth && context.m_Constraints.m_Width &&
            measured.m_Preferred.m_X + HorizontalMargin(child) > *context.m_Constraints.m_Width)
            measured =
                MeasureChild(context, i,
                             {ClampSize(child, measured,
                                        std::max(0.0f, *context.m_Constraints.m_Width - HorizontalMargin(child)), true),
                              {}},
                             context.m_DefiniteWidth);
        natural.push_back(measured);
        plan.m_Measure.m_Minimum.m_X =
            std::max(plan.m_Measure.m_Minimum.m_X, Minimum(child, measured, true) + HorizontalMargin(child));
        const float width = Basis(Item(child, measured, true)) + HorizontalMargin(child);
        if (rows.empty() || (context.m_Constraints.m_Width && !rows.back().empty() &&
                             used + context.m_Gap + width > *context.m_Constraints.m_Width + 0.001f))
        {
            rows.emplace_back();
            used = 0;
        }
        if (!rows.back().empty())
            used += context.m_Gap;
        rows.back().push_back(i);
        used += width;
    }
    float y = 0;
    for (const auto& row : rows)
    {
        LayoutContext line;
        line.m_Constraints.m_Width = context.m_Constraints.m_Width;
        line.m_DefiniteWidth = context.m_DefiniteWidth;
        line.m_DefiniteHeight = context.m_DefiniteHeight;
        line.m_Gap = context.m_Gap;
        line.m_Alignment = context.m_Alignment;
        line.m_Justification = context.m_Justification;
        for (const auto index : row)
            line.m_Children.push_back(context.m_Children[index]);
        line.m_Measure = [&](std::size_t index, MeasureConstraints constraints, bool definiteWidth, bool definiteHeight)
        {
            return constraints.m_Width || constraints.m_Height
                       ? MeasureChild(context, row[index], constraints, definiteWidth, definiteHeight)
                       : natural[row[index]];
        };
        auto layout = BoxLayout(Axis::Horizontal).Compute(line);
        for (auto& slot : layout.m_Slots)
        {
            slot.m_Index = row[slot.m_Index];
            slot.m_Bounds.m_Y += y;
            if (!plan.m_Measure.m_Baseline && slot.m_Measure.m_Baseline)
                plan.m_Measure.m_Baseline = slot.m_Bounds.m_Y + *slot.m_Measure.m_Baseline;
            plan.m_Slots.push_back(slot);
        }
        for (auto& issue : layout.m_Problems)
        {
            if (issue.m_Child)
                issue.m_Child = row[*issue.m_Child];
            plan.m_Problems.push_back(std::move(issue));
        }
        plan.m_Measure.m_Preferred.m_X = std::max(plan.m_Measure.m_Preferred.m_X, layout.m_Measure.m_Preferred.m_X);
        plan.m_Measure.m_Minimum.m_Y += layout.m_Measure.m_Minimum.m_Y;
        y += layout.m_Measure.m_Preferred.m_Y + context.m_Gap;
    }
    if (!rows.empty())
    {
        y -= context.m_Gap;
        plan.m_Measure.m_Minimum.m_Y += context.m_Gap * static_cast<float>(rows.size() - 1);
    }
    plan.m_Measure.m_Preferred.m_Y = y;
    plan.m_Measure.m_HeightForWidth = true;
    if (context.m_Constraints.m_Height && y > *context.m_Constraints.m_Height + 0.01f)
        plan.m_Problems.push_back({LayoutIssue::Overflow,
                                   Axis::Vertical,
                                   {},
                                   *context.m_Constraints.m_Height,
                                   y,
                                   "Wrapped rows exceed available height; row content and spacing are preserved."});
    return plan;
}
} // namespace EStudioUI
