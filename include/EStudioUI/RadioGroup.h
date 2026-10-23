#pragma once
#include "Widget.h"

namespace EStudioUI
{
class RadioButton;

//! A shared exclusive group observing members without owning widgets.
class RadioGroup : public std::enable_shared_from_this<RadioGroup>
{
  public:
    //! Creates a policy to be shared by radio buttons.
    RadioGroup();
    //! Prevents clearing an established selection.
    void SetSelectionRequired(bool required);
    //! Returns the clear-selection policy.
    bool IsSelectionRequired() const;
    //! Returns the live selected member, or null.
    RadioButton* GetCheckedButton() const;
    //! Selects a member, or clears an optional group with null.
    void Select(RadioButton* button);
    //! Reports a coherent group commit through a safe observer.
    Signal<WidgetRef>& SelectionChanged();

  private:
    friend class RadioButton;
    //! Registers a member after owner checks.
    void Add(RadioButton& button);
    //! Removes a member without callbacks during teardown.
    void Remove(RadioButton& button);
    //! Navigates eligible members in registration order.
    void Move(RadioButton& origin, bool backwards);
    //! Enforces group affinity.
    ThreadAffinity m_Affinity;
    //! Observes members without retaining them.
    std::vector<WidgetRef> m_Buttons;
    //! Observes the selected member.
    WidgetRef m_Selected;
    //! Prevents user clearing once selected.
    bool m_Required = true;
    //! Flattens reentrant requests.
    bool m_Changing = false;
    //! Retains the latest queued request.
    WidgetRef m_Queued;
    //! Distinguishes no request from a clear request.
    bool m_Pending = false;
    //! Publishes committed selection.
    Signal<WidgetRef> m_SelectionChanged;
};
} // namespace EStudioUI
