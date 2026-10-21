#pragma once

#include "Label.h"

namespace EStudioUI
{
//! A focusable button with pointer capture and keyboard activation.
class Button final : public Label
{
  public:
    //! Creates a button with a UTF-8 caption.
    explicit Button(std::string text = {});

    //! Returns whether the control participates in Tab navigation.
    bool IsFocusable() const override;

    //! Exposes the activation signal.
    Signal<>& Clicked();

  protected:
    //! Begins a captured pointer activation.
    void OnPointerDown(const PointerEvent& event) override;

    //! Updates pressed appearance as the captured pointer moves.
    void OnPointerMove(const PointerEvent& event) override;

    //! Activates only when the matching release is inside the control.
    void OnPointerUp(const PointerEvent& event) override;

    //! Supports Space press/release and Enter activation.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Cancels keyboard activation when focus is lost.
    void OnFocus(bool focused) override;

    //! Cancels pointer and keyboard activation.
    void OnCaptureLost() override;

  private:
    //! Tracks an outstanding pointer press.
    bool m_PointerDown = false;

    //! Tracks an outstanding Space-key press.
    bool m_SpaceDown = false;

    //! Delivers activation after control state has been committed.
    Signal<> m_Clicked;
};
} // namespace EStudioUI
