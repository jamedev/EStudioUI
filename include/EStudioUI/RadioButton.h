#pragma once
#include "CheckBox.h"
#include "RadioGroup.h"

namespace EStudioUI
{
//! A radio choice with explicit grouping and arrow-key navigation.
class RadioButton final : public CheckBox
{
  public:
    //! Creates an unchecked radio, optionally sharing a group.
    explicit RadioButton(std::string text = {}, std::shared_ptr<RadioGroup> group = {});
    //! Removes its weak registration.
    ~RadioButton() override;
    //! Changes grouping; null creates an independent choice.
    void SetGroup(std::shared_ptr<RadioGroup> group);
    //! Returns the shared grouping policy.
    std::shared_ptr<RadioGroup> GetGroup() const;
    //! Selects through the group; indeterminate is rejected.
    void SetCheckState(CheckState state) override;
    //! Rejects independent toggle actions; use a group for exclusivity.
    void SetAction(std::shared_ptr<Action> action) override;

  protected:
    //! Advertises radio selection.
    SemanticInfo DescribeSemantics() const override;
    //! Selects through the canonical group path.
    SemanticStatus OnSemanticAction(const SemanticCommand& command) override;
    //! User activation always selects.
    CheckState NextCheckState() const override;
    //! Navigates eligible members with arrow keys.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    friend class RadioGroup;
    //! Shares policy without retaining other buttons.
    std::shared_ptr<RadioGroup> m_Group;
};
} // namespace EStudioUI
