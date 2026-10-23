#pragma once
#include "FontFace.h"
#include "Widget.h"

namespace EStudioUI
{
//! Bounded progress with rich track, chunk and label parts.
class ProgressBar : public Widget
{
  public:
    //! Creates zero progress in the range 0..100.
    explicit ProgressBar(std::string type = "progressbar");
    //! Replaces finite increasing limits and clamps the current value.
    void SetRange(double minimum, double maximum);
    //! Clamps a finite value to the configured range.
    void SetValue(double value);
    //! Returns current progress.
    double GetValue() const;
    //! Returns the lower bound.
    double GetMinimum() const;
    //! Returns the upper bound.
    double GetMaximum() const;
    //! Enables unknown-progress motion.
    void SetIndeterminate(bool enabled);
    //! Returns whether progress is unknown.
    bool IsIndeterminate() const;
    //! Enables the percentage caption.
    void SetTextVisible(bool visible);
    //! Reports committed value changes.
    Signal<double>& ValueChanged();

  protected:
    //! Supplies read-only range metadata.
    SemanticInfo DescribeSemantics() const override;
    //! Measures a bounded preferred viewport.
    Point Measure(FontFace& font) const override;
    //! Requests frames only for an enabled busy control.
    bool NeedsContinuousRendering() const override;
    //! Retires cached caption resources.
    void OnFontsChanged() noexcept override;
    //! Paints track, progress and cached percentage text.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

  private:
    //! Inclusive lower bound.
    double m_Minimum = 0;
    //! Inclusive upper bound.
    double m_Maximum = 100;
    //! Canonical progress.
    double m_Value = 0;
    //! Enables unknown-progress animation.
    bool m_Indeterminate = false;
    //! Enables percentage text.
    bool m_TextVisible = true;
    //! Retains the displayed percentage.
    mutable int m_Percent = -1;
    //! Owns the caption layout.
    mutable TextLayout m_Caption;
    //! Flattens reentrant value notification.
    bool m_Notifying = false;
    //! Stores the last announced value.
    double m_Notified = 0;
    //! Publishes value changes.
    Signal<double> m_ValueChanged;
};
} // namespace EStudioUI
