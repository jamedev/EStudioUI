#pragma once

#include "LayoutStrategy.h"

namespace EStudioUI
{
//! Wraps consecutive children into rows using their content/fixed/minimum widths.
class FlowLayout final : public LayoutStrategy
{
  public:
    //! Returns Flow for diagnostic reports.
    std::string_view GetName() const override;
    //! Wraps rows, then reuses bounded row allocation and baseline alignment.
    LayoutPlan Compute(const LayoutContext& context) const override;
};
} // namespace EStudioUI
