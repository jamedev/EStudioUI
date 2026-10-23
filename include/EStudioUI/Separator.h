#pragma once
#include "Widget.h"

namespace EStudioUI
{
//! A CSS-styled horizontal or vertical visual separator.
class Separator final : public Widget
{
  public:
    //! Creates a horizontal separator by default.
    explicit Separator(Axis axis = Axis::Horizontal);
    //! Changes orientation without imposing a code size.
    void SetOrientation(Axis axis);
    //! Returns line orientation.
    Axis GetOrientation() const;

  protected:
    //! Supplies natural line thickness.
    Point Measure(FontFace& font) const override;
    //! Advertises noninteractive structure.
    SemanticInfo DescribeSemantics() const override;

  private:
    //! Stores line orientation.
    Axis m_Axis = Axis::Horizontal;
};
} // namespace EStudioUI
