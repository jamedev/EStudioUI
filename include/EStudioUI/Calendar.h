#pragma once
#include "Widget.h"
#include "TemporalValue.h"
#include "FontFace.h"
#include <array>

namespace EStudioUI
{
//! A fixed-cost Gregorian calendar with GPU day cells and virtual accessibility nodes.
class Calendar : public Widget
{
  public:
    //! Creates a calendar showing January 2000 with its first day selected.
    Calendar();
    //! Selects a valid civil date and reveals its month.
    void SetDate(CalendarDate date);
    //! Returns the selected valid date.
    CalendarDate GetDate() const;
    //! Moves the displayed month within supported year bounds.
    void MoveMonth(int delta);
    //! Reports selection changes.
    Signal<CalendarDate>& DateChanged();
    //! Reports explicit day acceptance, including reselecting the current date.
    Signal<CalendarDate>& Activated();
    //! Supports keyboard traversal.
    bool IsFocusable() const override;

  protected:
    //! Supplies a compact seven-column preferred size.
    Point Measure(FontFace& font) const override;
    //! Paints a bounded month without creating per-day widgets.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;
    //! Resolves previous/next month or day selection.
    void OnPointerDown(const PointerEvent& event) override;
    //! Moves days/weeks/months or accepts the current day.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;
    //! Describes the calendar as a list of selectable dates.
    SemanticInfo DescribeSemantics() const override;
    //! Reports the current month's days plus navigation commands.
    std::size_t GetSemanticChildCount(SemanticKey parent) const override;
    //! Resolves stable date identities.
    std::optional<SemanticKey> GetSemanticChildAt(SemanticKey parent, std::size_t index) const override;
    //! Resolves the current sibling index of a date identity.
    std::optional<std::size_t> GetSemanticChildIndex(SemanticKey key) const override;
    //! Describes a day or navigation command.
    std::optional<SemanticVirtualNode> DescribeSemanticChild(SemanticKey key) const override;
    //! Resolves the selected day as logical focus.
    std::optional<SemanticKey> GetSemanticFocus() const override;
    //! Returns one selected date when its month is displayed.
    std::size_t GetSemanticSelectionCount() const override;
    //! Returns a bounded selected-date page.
    std::vector<SemanticKey> GetSemanticSelection(std::size_t offset, std::size_t limit) const override;
    //! Routes semantic selection and invocation to canonical date methods.
    SemanticStatus OnSemanticChildAction(SemanticKey key, const SemanticCommand& command) override;

  private:
    //! Returns the first weekday column using Monday as zero.
    unsigned FirstColumn() const;
    //! Returns the number of days in the displayed month.
    unsigned DayCount() const;
    //! Computes header and day geometry, using zero for previous and 32 for next.
    Rect Cell(unsigned day) const;
    //! Selected value.
    CalendarDate m_Date;
    //! Displayed month, with day fixed to one.
    CalendarDate m_Month;
    //! Changes when the displayed month's logical children change.
    std::uint64_t m_Revision = 1;
    //! Cached header and day text.
    mutable std::array<TextLayout, 43> m_Text;
    //! Exact cached captions.
    mutable std::array<std::string, 43> m_Captions;
    //! Selection observers.
    Signal<CalendarDate> m_DateChanged;
    //! Explicit acceptance observers.
    Signal<CalendarDate> m_Activated;
};
} // namespace EStudioUI
