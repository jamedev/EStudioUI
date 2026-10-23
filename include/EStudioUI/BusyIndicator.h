#pragma once
#include "ProgressBar.h"

namespace EStudioUI
{
//! A compact animated unknown-progress bar sharing the progress contract.
class BusyIndicator final : public ProgressBar
{
  public:
    //! Creates a running indicator without percentage text.
    BusyIndicator();

  protected:
    //! Measures a compact status bar.
    Point Measure(FontFace& font) const override;
};
} // namespace EStudioUI
