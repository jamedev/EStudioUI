#pragma once
#include "DateEdit.h"
#include "TimeEdit.h"

namespace EStudioUI
{
//! Civil date and local clock composition publishing only complete optional values.
class DateTimeEdit : public Widget
{
  public:
    //! Creates empty date and time fields.
    DateTimeEdit();
    //! Sets both components coherently, or clears both.
    void SetValue(std::optional<DateTimeValue> value);
    //! Returns the last complete committed pair independently of partial edits.
    std::optional<DateTimeValue> GetValue() const;
    //! Validates both drafts before committing either; partial/invalid pairs return false.
    bool Commit();
    //! Returns the date component editor.
    DateEdit& GetDateEditor() const;
    //! Returns the clock component editor.
    TimeEdit& GetTimeEditor() const;
    //! Reports coherent complete optional pairs.
    Signal<std::optional<DateTimeValue>>& ValueChanged();

  private:
    //! Publishes only when both component values exist or both fields are empty.
    void ComponentsChanged();
    //! Date component.
    DateEdit* m_Date = nullptr;
    //! Time component.
    TimeEdit* m_Time = nullptr;
    //! Last complete pair.
    std::optional<DateTimeValue> m_Value;
    //! Suppresses partial programmatic pair notifications.
    bool m_Syncing = false;
    //! Pair observers.
    Signal<std::optional<DateTimeValue>> m_ValueChanged;
    //! Bounds synchronous pair notifications.
    unsigned m_NotificationDepth = 0;
};
} // namespace EStudioUI
