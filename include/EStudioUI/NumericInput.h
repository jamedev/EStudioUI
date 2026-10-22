#pragma once

#include "TextBox.h"
#include "ToolButton.h"

namespace EStudioUI
{
//! A composable validated numeric editor with decrement and increment commands.
class NumericInput : public Widget
{
  public:
    //! Creates a finite scalar editor with six displayed decimal places.
    explicit NumericInput(double value = 0);

    //! Sets finite inclusive bounds and clamps the committed value.
    void SetRange(double minimum, double maximum);

    //! Returns the inclusive minimum.
    double GetMinimum() const;

    //! Returns the inclusive maximum.
    double GetMaximum() const;

    //! Sets the positive finite increment used by buttons, keys, and wheel input.
    void SetStep(double step);

    //! Returns the configured increment.
    double GetStep() const;

    //! Sets displayed decimal places from zero through twelve without changing stored precision.
    void SetDecimals(int decimals);

    //! Returns the displayed decimal count.
    int GetDecimals() const;

    //! Clamps a finite value and refreshes its text, emitting only for an actual value change.
    void SetValue(double value);

    //! Returns the committed value independently of an intermediate editing string.
    double GetValue() const;

    //! Returns the ordinary embedded editor for composition and custom styling.
    TextBox& GetEditor();

    //! Returns read-only access to the embedded editor.
    const TextBox& GetEditor() const;

    //! Commits acceptable text or restores the previous value and returns false.
    bool Commit();

    //! Steps from the last valid committed value, clamping safely to the range.
    void StepBy(int steps);

    //! Emits after committed value changes, including programmatic changes.
    Signal<double>& ValueChanged();

    //! Emits after successful Enter/focus-loss commits with the resulting value.
    Signal<double>& EditingFinished();

  protected:
    //! Intercepts arrows and Escape while the embedded editor owns focus.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Steps only while the embedded editor has keyboard focus.
    bool OnScroll(Point delta, Modifiers modifiers) override;

  private:
    //! Formats the committed value using a locale-independent decimal separator.
    std::string FormatValue() const;

    //! Restores the embedded editor text; callbacks may destroy this control.
    void RefreshText();

    //! Rebuilds the shared immutable numeric validation policy.
    void RefreshValidator();

    //! Mirrors the embedded editor's validation state on the composed control.
    void UpdateValidationState(ValidationState state);

    //! Observes the owned single-line editor.
    TextBox* m_Editor = nullptr;

    //! Observes the owned decrement command.
    ToolButton* m_Decrement = nullptr;

    //! Observes the owned increment command.
    ToolButton* m_Increment = nullptr;

    //! Inclusive minimum.
    double m_Minimum = -1e12;

    //! Inclusive maximum.
    double m_Maximum = 1e12;

    //! Committed finite value.
    double m_Value = 0;

    //! Positive stepping increment.
    double m_Step = 1;

    //! Fixed decimal display precision.
    int m_Decimals = 6;

    //! Invalidates outer emissions when text callbacks perform a newer update.
    std::uint64_t m_ValueRevision = 0;

    //! Reports committed changes.
    Signal<double> m_ValueChanged;

    //! Reports successful editing completion.
    Signal<double> m_EditingFinished;
};
} // namespace EStudioUI
