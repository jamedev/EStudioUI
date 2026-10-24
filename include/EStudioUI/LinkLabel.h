#pragma once
#include "SelectableLabel.h"

namespace EStudioUI
{
//! Selectable link text; activation reports its target to the application without implicit OS execution.
class LinkLabel : public SelectableLabel
{
  public:
    //! Creates a link with a caption and application-routed target.
    explicit LinkLabel(std::string text = {}, std::string target = {});
    //! Sets an owned target of at most 8192 bytes.
    void SetTarget(std::string target);
    //! Returns the target passed on activation.
    const std::string& GetTarget() const;
    //! Reports mouse/Enter activation; selection drags do not activate.
    Signal<std::string>& Activated();

  protected:
    //! Starts ordinary read-only selection and remembers click geometry.
    void OnPointerDown(const PointerEvent& event) override;
    //! Tracks selection drags independently of their final position.
    void OnPointerMove(const PointerEvent& event) override;
    //! Cancels a pending link click.
    void OnCaptureLost() override;
    //! Finishes selection before resolving a non-drag activation.
    void OnPointerUp(const PointerEvent& event) override;
    //! Activates on Enter and preserves selection/copy commands.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;
    //! Exposes an invokable read-only link.
    SemanticInfo DescribeSemantics() const override;
    //! Routes semantic invocation through the same application signal.
    SemanticStatus OnSemanticAction(const SemanticCommand& command) override;

  private:
    //! Application target, never interpreted as a shell command.
    std::string m_Target;
    //! Pointer press position.
    Point m_Press;
    //! True after a selection gesture exceeds the click threshold.
    bool m_Dragged = false;
    //! Activation requires a matching press.
    bool m_Pressed = false;
    //! Activation observers.
    Signal<std::string> m_Activated;
};
} // namespace EStudioUI
