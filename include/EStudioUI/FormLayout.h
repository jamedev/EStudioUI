#pragma once

#include "GridLayout.h"

namespace EStudioUI
{
//! Pairs consecutive label/field children in two shared, baseline-aligned columns.
class FormLayout final : public LayoutStrategy
{
  public:
    //! Creates a content label column and a growing field column by default.
    explicit FormLayout(SizeRule labels = SizeRule::Content(0, 320), SizeRule fields = SizeRule::Fill());
    //! Returns Form for diagnostic reports.
    std::string_view GetName() const override;
    //! Reuses grid allocation while aligning each label/field baseline.
    LayoutPlan Compute(const LayoutContext& context) const override;

  private:
    //! Shared track implementation.
    GridLayout m_Grid;
};
} // namespace EStudioUI
