#pragma once
#include "RadioButton.h"

namespace EStudioUI
{
//! Compact, exclusive choices reusing radio semantics, focus and arrow navigation.
class SegmentedControl : public Widget
{
  public:
    //! Creates an empty control.
    SegmentedControl();
    //! Replaces at most 64 bounded captions and selects the first entry.
    void SetItems(std::vector<std::string> items);
    //! Selects a segment; minus one clears an optional empty control.
    void SetCurrentIndex(int index);
    //! Returns the selected segment or minus one.
    int GetCurrentIndex() const;
    //! Reports committed selection changes.
    Signal<int>& CurrentChanged();

  private:
    //! Shared canonical exclusivity policy.
    std::shared_ptr<RadioGroup> m_Group;
    //! Composed bounded segment buttons.
    std::vector<RadioButton*> m_Buttons;
    //! Suppresses partial rebuilding notifications.
    bool m_Rebuilding = false;
    //! Selection observers.
    Signal<int> m_CurrentChanged;
};
} // namespace EStudioUI
