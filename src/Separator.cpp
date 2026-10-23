#include <EStudioUI/Separator.h>
#include <stdexcept>

namespace EStudioUI
{
Separator::Separator(Axis axis) : Widget("separator")
{
    SetOrientation(axis);
}

void Separator::SetOrientation(Axis axis)
{
    VerifyStructuralMutation();
    if (axis != Axis::Horizontal && axis != Axis::Vertical)
        throw std::invalid_argument("Invalid separator orientation.");
    m_Axis = axis;
    SetState(Horizontal, axis == Axis::Horizontal);
    SetState(Vertical, axis == Axis::Vertical);
    Invalidate(Invalidation::Content);
}

Axis Separator::GetOrientation() const
{
    VerifyAccess();
    return m_Axis;
}

Point Separator::Measure(FontFace&) const
{
    return m_Axis == Axis::Horizontal ? Point{24, 1} : Point{1, 24};
}

SemanticInfo Separator::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Role = SemanticRole::Separator;
    return info;
}
} // namespace EStudioUI
