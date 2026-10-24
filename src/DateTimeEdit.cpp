#include <EStudioUI/DateTimeEdit.h>
#include "PropertyNotification.h"
#include <stdexcept>

namespace EStudioUI
{
DateTimeEdit::DateTimeEdit() : Widget("datetimeedit")
{
    SetLayoutDirection(Axis::Horizontal);
    m_Date = &Add<DateEdit>();
    m_Date->SetWidth(SizeRule::Fill());
    m_Time = &Add<TimeEdit>();
    m_Time->SetWidth(SizeRule::Fill());
    const WidgetRef self(this);
    m_Date->DateChanged().Connect(
        [self](std::optional<CalendarDate>)
        {
            if (auto* live = dynamic_cast<DateTimeEdit*>(self.Get()); live && !live->m_Syncing)
            {
                live->ComponentsChanged();
            }
        });
    m_Time->TimeChanged().Connect(
        [self](std::optional<ClockTime>)
        {
            if (auto* live = dynamic_cast<DateTimeEdit*>(self.Get()); live && !live->m_Syncing)
            {
                live->ComponentsChanged();
            }
        });
}

void DateTimeEdit::SetValue(std::optional<DateTimeValue> value)
{
    VerifyStructuralMutation();
    if (value && !value->IsValid())
    {
        throw std::invalid_argument("DateTimeEdit requires a valid civil date and local time.");
    }
    const bool changed = m_Value != value;
    m_Value = value;
    const WidgetRef self(this);
    {
        Detail::PropertySyncScope scope(*this, m_Syncing);
        m_Date->SetDate(value ? std::optional{value->m_Date} : std::nullopt);
        if (!self.Get())
        {
            return;
        }
        m_Time->SetTime(value ? std::optional{value->m_Time} : std::nullopt);
    }
    if (auto* live = dynamic_cast<DateTimeEdit*>(self.Get()); live && changed && live->m_Value == value)
    {
        Detail::EmitPropertyNotification(*live, live->m_NotificationDepth, live->m_ValueChanged, value);
    }
}

std::optional<DateTimeValue> DateTimeEdit::GetValue() const
{
    VerifyAccess();
    return m_Value;
}

bool DateTimeEdit::Commit()
{
    VerifyStructuralMutation();
    const auto dateText = m_Date->GetEditor().GetText(), timeText = m_Time->GetEditor().GetText();
    if (dateText.empty() && timeText.empty())
    {
        SetValue({});
        return true;
    }
    const auto date = CalendarDate::Parse(dateText);
    const auto time = ClockTime::Parse(timeText);
    if (!date || !time)
    {
        return false;
    }
    SetValue(DateTimeValue{*date, *time});
    return true;
}

void DateTimeEdit::ComponentsChanged()
{
    const auto date = m_Date->GetDate();
    const auto time = m_Time->GetTime();
    std::optional<DateTimeValue> value;
    if (date && time)
    {
        value = DateTimeValue{*date, *time};
    }
    else if (date || time)
    {
        return;
    }
    if (value == m_Value)
    {
        return;
    }
    m_Value = value;
    Detail::EmitPropertyNotification(*this, m_NotificationDepth, m_ValueChanged, value);
}

DateEdit& DateTimeEdit::GetDateEditor() const
{
    VerifyAccess();
    return *m_Date;
}

TimeEdit& DateTimeEdit::GetTimeEditor() const
{
    VerifyAccess();
    return *m_Time;
}

Signal<std::optional<DateTimeValue>>& DateTimeEdit::ValueChanged()
{
    VerifyAccess();
    return m_ValueChanged;
}
} // namespace EStudioUI
