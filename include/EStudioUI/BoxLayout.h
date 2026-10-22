#pragma once

#include "LayoutStrategy.h"

namespace EStudioUI
{
//! A row or column using the shared bounded allocator.
class BoxLayout final : public LayoutStrategy
{
  public:
    //! Creates a row or column without changing child ownership.
    explicit BoxLayout(Axis axis = Axis::Vertical);
    //! Returns Row or Column for diagnostic reports.
    std::string_view GetName() const override;
    //! Measures allocated widths before distributing heights and aligning baselines.
    LayoutPlan Compute(const LayoutContext& context) const override;

  private:
    //! Flow direction.
    Axis m_Axis;
};
} // namespace EStudioUI
