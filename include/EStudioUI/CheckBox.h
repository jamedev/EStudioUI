#pragma once
#include "Button.h"

namespace EStudioUI
{
//! Persistent states of a checkbox.
enum class CheckState
{
    Unchecked,
    Checked,
    Indeterminate
};

//! A keyboard-operable choice with GPU indicator, mark and caption parts.
class CheckBox : public Button
{
  public:
    //! Creates an unchecked control with a single-line caption.
    explicit CheckBox(std::string text = {}, std::string type = "checkbox");
    //! Associates a checkable boolean command; three-state cycling uses standalone state.
    void SetAction(std::shared_ptr<Action> action) override;
    //! Includes indeterminate in the user activation cycle.
    void SetTristate(bool enabled);
    //! Returns the user cycle policy.
    bool IsTristate() const;
    //! Commits a complete persistent state.
    virtual void SetCheckState(CheckState state);
    //! Returns the complete persistent state.
    CheckState GetCheckState() const;
    //! Commits a boolean state through the canonical path.
    void SetChecked(bool checked);
    //! Returns true only for the checked state.
    bool IsChecked() const;
    //! Reports a committed complete state.
    Signal<CheckState>& StateChanged();
    //! Reports changes in the boolean checked value.
    Signal<bool>& Toggled();

  protected:
    friend class RadioGroup;
    //! Supplies toggle metadata.
    SemanticInfo DescribeSemantics() const override;
    //! Resolves user activation.
    virtual CheckState NextCheckState() const;
    //! Commits presentation before optional notification.
    void ApplyCheckState(CheckState state, bool notify = true);
    //! Delivers bounded, deletion-safe notifications.
    void NotifyCheckState();
    //! Mirrors the associated boolean action.
    void OnActionChanged() override;
    //! Retires caption resources.
    void OnFontsChanged() noexcept override;
    //! Measures indicator and caption.
    Point Measure(FontFace& font) const override;
    //! Reports the painted baseline.
    MeasureResult MeasureConstrained(FontFace& font, const MeasureConstraints& constraints) const override;
    //! Paints CSS parts and analytic marks.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

  private:
    //! Returns the cached single-line caption.
    const TextLayout& Caption(FontFace& font) const;
    //! Chooses radio marker geometry.
    bool m_Radio = false;
    //! Stores the persistent state.
    CheckState m_CheckState = CheckState::Unchecked;
    //! Stores the last announced state.
    CheckState m_Notified = CheckState::Unchecked;
    //! Enables the three-state activation cycle.
    bool m_Tristate = false;
    //! Flattens recursive observers.
    bool m_Notifying = false;
    //! Owns cached shaped text.
    mutable TextLayout m_Caption;
    //! Retains exact caption bytes.
    mutable std::string m_CaptionText;
    //! Publishes complete state updates.
    Signal<CheckState> m_StateChanged;
    //! Publishes boolean updates.
    Signal<bool> m_Toggled;
};
} // namespace EStudioUI
