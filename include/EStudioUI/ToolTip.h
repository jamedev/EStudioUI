#pragma once
#include "Label.h"

namespace EStudioUI
{
//! Noninteractive wrapping help, presented by Widget::SetToolTip and the UI scheduler.
class ToolTip final : public Label
{
  public:
    //! Creates GPU-rendered wrapping help text.
    explicit ToolTip(std::string text = {});

  protected:
    //! Advertises transient help semantics.
    SemanticInfo DescribeSemantics() const override;
};
} // namespace EStudioUI
