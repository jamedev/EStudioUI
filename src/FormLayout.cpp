#include <EStudioUI/FormLayout.h>

namespace EStudioUI
{
FormLayout::FormLayout(SizeRule labels, SizeRule fields) : m_Grid({labels, fields})
{
}

std::string_view FormLayout::GetName() const
{
    return "Form";
}

LayoutPlan FormLayout::Compute(const LayoutContext& context) const
{
    auto aligned = context;
    aligned.m_Alignment = LayoutAlignment::Baseline;
    return m_Grid.Compute(aligned);
}
} // namespace EStudioUI
