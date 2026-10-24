#pragma once
#include "TemporalValue.h"
#include "TextBox.h"

namespace EStudioUI
{
//! Validated optional local-time input with second-wise keyboard stepping.
class TimeEdit : public Widget
{
  public:
    //! Creates an empty optional time editor.
    TimeEdit();
    //! Sets a valid clock value or clears the field.
    void SetTime(std::optional<ClockTime> time);
    //! Returns the committed time independently of unfinished input.
    std::optional<ClockTime> GetTime() const;
    //! Commits a complete valid value or empty field; preserves incomplete drafts.
    bool Commit();
    //! Returns the composed text editor for validation and native input.
    TextBox& GetEditor() const;
    //! Opens an owned hour/minute/second picker with explicit Apply/Cancel.
    void ShowPicker();
    //! Reports coherent optional time values.
    Signal<std::optional<ClockTime>>& TimeChanged();

  protected:
    //! Steps one second with Up/Down, clamping at midnight boundaries.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Embedded field.
    TextBox* m_Editor = nullptr;
    //! Last committed time.
    std::optional<ClockTime> m_Time;
    //! Logically owned picker popup.
    WidgetRef m_Popup;
    //! Suppresses text refresh feedback.
    bool m_Syncing = false;
    //! Value observers.
    Signal<std::optional<ClockTime>> m_TimeChanged;
    //! Bounds observer feedback.
    unsigned m_NotificationDepth = 0;
};
} // namespace EStudioUI
