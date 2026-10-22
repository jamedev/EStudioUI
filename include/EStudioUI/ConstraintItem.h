#pragma once

#include "Constraint.h"

namespace EStudioUI
{
class Widget;

//! Stable anchors for an immediate child, independent of its address and ownership index.
class ConstraintItem
{
  public:
    //! Captures a widget identity on its owner thread, without retaining the widget.
    explicit ConstraintItem(const Widget& widget);
    //! Returns anchors for the container's inner content rectangle.
    static ConstraintItem Parent();
    //! Returns the left border edge.
    ConstraintExpression Left() const;
    //! Returns the right border edge.
    ConstraintExpression Right() const;
    //! Returns the top border edge.
    ConstraintExpression Top() const;
    //! Returns the bottom border edge.
    ConstraintExpression Bottom() const;
    //! Returns the horizontal center.
    ConstraintExpression CenterX() const;
    //! Returns the vertical center.
    ConstraintExpression CenterY() const;
    //! Returns the outer width, excluding margins.
    ConstraintExpression Width() const;
    //! Returns the outer height, excluding margins.
    ConstraintExpression Height() const;

  private:
    //! Constructs container anchors.
    ConstraintItem() = default;
    //! Returns one primitive coordinate.
    ConstraintExpression Coordinate(ConstraintCoordinate coordinate) const;
    //! Stable identity, or zero for the parent.
    std::uint64_t m_Target = 0;
};
} // namespace EStudioUI
