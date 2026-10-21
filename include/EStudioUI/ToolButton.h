#pragma once

#include "Button.h"

namespace EStudioUI
{
//! A compact command button with optional persistent checked state.
class ToolButton : public Button
{
  public:
    //! Creates a UTF-8 text/icon command button.
    explicit ToolButton(std::string text = {});

    //! Enables automatic checked-state toggling on activation.
    void SetCheckable(bool checkable);

    //! Returns whether activation toggles the checked state.
    bool IsCheckable() const;

    //! Updates the checked state and CSS :checked pseudo-class.
    void SetChecked(bool checked);

    //! Returns persistent checked state.
    bool IsChecked() const;

    //! Emits after a programmatic or user checked-state change.
    Signal<bool>& Toggled();

  private:
    //! Enables toggle behavior independently of the current value.
    bool m_Checkable = false;

    //! Stores persistent checked state.
    bool m_Checked = false;

    //! Reports committed checked-state changes.
    Signal<bool> m_Toggled;
};
} // namespace EStudioUI
