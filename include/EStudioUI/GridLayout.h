#pragma once

#include "LayoutStrategy.h"

namespace EStudioUI
{
//! Row-major tracks with shared column sizing and optional explicit row rules.
class GridLayout final : public LayoutStrategy
{
  public:
    //! Creates nonempty columns; unspecified rows use Content sizing.
    explicit GridLayout(std::vector<SizeRule> columns = {SizeRule::Fill()}, std::vector<SizeRule> rows = {});
    //! Returns Grid for diagnostic reports.
    std::string_view GetName() const override;
    //! Allocates columns, measures cell widths, then allocates and aligns rows.
    LayoutPlan Compute(const LayoutContext& context) const override;

  private:
    //! Immutable column rules.
    std::vector<SizeRule> m_Columns;
    //! Explicit row rules followed by implicit Content rows.
    std::vector<SizeRule> m_Rows;
};
} // namespace EStudioUI
