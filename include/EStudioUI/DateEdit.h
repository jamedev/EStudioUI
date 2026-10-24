#pragma once
#include "Calendar.h"
#include "TextBox.h"
#include "ToolButton.h"

namespace EStudioUI
{
//! Validated ISO civil-date input with an asynchronously presented calendar.
class DateEdit : public Widget
{
  public:
    //! Creates an empty optional date editor.
    DateEdit();
    //! Sets a valid date or clears it; invalid candidates leave the control unchanged.
    void SetDate(std::optional<CalendarDate> date);
    //! Returns the last committed optional date independently of unfinished input.
    std::optional<CalendarDate> GetDate() const;
    //! Commits a valid date or empty field, retaining incomplete drafts on failure.
    bool Commit();
    //! Returns the ordinary editor, including its validator and native text support.
    TextBox& GetEditor() const;
    //! Opens an owned calendar popup below the field.
    void ShowCalendar();
    //! Reports committed optional dates.
    Signal<std::optional<CalendarDate>>& DateChanged();

  private:
    //! Embedded canonical text editor.
    TextBox* m_Editor = nullptr;
    //! Committed optional value.
    std::optional<CalendarDate> m_Date;
    //! Logically owned calendar popup.
    WidgetRef m_Popup;
    //! Suppresses text synchronization feedback.
    bool m_Syncing = false;
    //! Value observers.
    Signal<std::optional<CalendarDate>> m_DateChanged;
    //! Bounds synchronous notification recursion.
    unsigned m_NotificationDepth = 0;
};
} // namespace EStudioUI
