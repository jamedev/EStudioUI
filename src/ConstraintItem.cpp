#include <EStudioUI/ConstraintItem.h>
#include <EStudioUI/Widget.h>

namespace EStudioUI
{
ConstraintItem::ConstraintItem(const Widget& widget)
{
    widget.VerifyAccess();
    m_Target = widget.m_TargetKey;
}

ConstraintItem ConstraintItem::Parent()
{
    return {};
}

ConstraintExpression ConstraintItem::Coordinate(ConstraintCoordinate coordinate) const
{
    return ConstraintExpression(ConstraintTerm{m_Target, coordinate, 1});
}

ConstraintExpression ConstraintItem::Left() const
{
    return Coordinate(ConstraintCoordinate::X);
}

ConstraintExpression ConstraintItem::Right() const
{
    return Left() + Width();
}

ConstraintExpression ConstraintItem::Top() const
{
    return Coordinate(ConstraintCoordinate::Y);
}

ConstraintExpression ConstraintItem::Bottom() const
{
    return Top() + Height();
}

ConstraintExpression ConstraintItem::CenterX() const
{
    return Left() + Width() * 0.5;
}

ConstraintExpression ConstraintItem::CenterY() const
{
    return Top() + Height() * 0.5;
}

ConstraintExpression ConstraintItem::Width() const
{
    return Coordinate(ConstraintCoordinate::Width);
}

ConstraintExpression ConstraintItem::Height() const
{
    return Coordinate(ConstraintCoordinate::Height);
}
} // namespace EStudioUI
