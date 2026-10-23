#pragma once

#include "Label.h"
#include "Action.h"

namespace EStudioUI
{
//! A focusable button with pointer capture and keyboard activation.
class Button : public Label
{
  public:
    //! Creates a button with a UTF-8 caption.
    explicit Button(std::string text = {}, std::string type = "button");

    //! Returns whether the control participates in Tab navigation.
    bool IsFocusable() const override;

    //! Exposes the activation signal.
    Signal<>& Clicked();

    //! Associates shared text/enabled/visible state and window-scoped shortcuts; null unbinds.
    virtual void SetAction(std::shared_ptr<Action> action);

    //! Returns the associated command, or null for a standalone button.
    std::shared_ptr<Action> GetAction() const;

    //! Activates an eligible visible button through the same path as pointer/keyboard input.
    bool Trigger();

  protected:
    //! Synchronizes committed action presentation; derived controls may extend checked-state handling.
    virtual void OnActionChanged();

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
    //! Emits legacy listeners before executing the captured shared command.
    void Activate();

    //! Owns the associated command without retaining other button presentations.
    std::shared_ptr<Action> m_Action;

    //! Disconnects canonical-state observation before this button's base is destroyed.
    std::optional<ScopedConnection> m_ActionConnection;

    //! Disables this presentation if an external command state domain expires first.
    std::unique_ptr<std::stop_callback<std::function<void()>>> m_ActionStop;

    //! Tracks an outstanding pointer press.
    bool m_PointerDown = false;

    //! Tracks an outstanding Space-key press.
    bool m_SpaceDown = false;

    //! Delivers activation after control state has been committed.
    Signal<> m_Clicked;
};
} // namespace EStudioUI
