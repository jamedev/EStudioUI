#include "PickerButton.h"
#include <EStudioUI/DateEdit.h>
#include <EStudioUI/Popup.h>
#include <EStudioUI/UiContext.h>
#include "PropertyNotification.h"
#include <stdexcept>

namespace EStudioUI
{
namespace
{
class DateValidator final : public InputValidator
{
  public:
    ValidationState Validate(std::string_view text) const override
    {
        if (text.empty() || CalendarDate::Parse(text))
        {
            return ValidationState::Acceptable;
        }
        if (text.size() > 10)
        {
            return ValidationState::Invalid;
        }
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if ((i == 4 || i == 7) ? text[i] != '-' : text[i] < '0' || text[i] > '9')
            {
                return ValidationState::Invalid;
            }
        }
        return ValidationState::Intermediate;
    }
};
} // namespace

DateEdit::DateEdit() : Widget("dateedit")
{
    SetLayoutDirection(Axis::Horizontal);
    SetGap(0);
    m_Editor = &Add<TextBox>();
    m_Editor->SetWidth(SizeRule::Fill());
    m_Editor->SetMaxBytes(10);
    m_Editor->SetPlaceholder("YYYY-MM-DD");
    m_Editor->SetValidator(std::make_shared<DateValidator>());
    auto& button = Add<Detail::PickerButton>(Detail::PickerButton::Symbol::Calendar);
    button.SetAccessibleName("Choose date");
    button.SetWidth(SizeRule::Fixed(32));
    const WidgetRef self(this);
    button.Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<DateEdit*>(self.Get()))
            {
                live->ShowCalendar();
            }
        });
    m_Editor->Submitted().Connect(
        [self](const std::string&)
        {
            if (auto* live = dynamic_cast<DateEdit*>(self.Get()))
            {
                live->Commit();
            }
        });
    m_Editor->FocusLost().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<DateEdit*>(self.Get()))
            {
                live->Commit();
            }
        });
}

void DateEdit::SetDate(std::optional<CalendarDate> date)
{
    VerifyStructuralMutation();
    if (date && !date->IsValid())
    {
        throw std::invalid_argument("DateEdit requires a valid Gregorian date.");
    }
    const bool changed = date != m_Date;
    m_Date = date;
    const WidgetRef self(this);
    {
        Detail::PropertySyncScope scope(*this, m_Syncing);
        m_Editor->SetText(date ? date->ToString() : "");
    }
    if (auto* live = dynamic_cast<DateEdit*>(self.Get()); live && changed && live->m_Date == date)
    {
        Detail::EmitPropertyNotification(*live, live->m_NotificationDepth, live->m_DateChanged, date);
    }
}

std::optional<CalendarDate> DateEdit::GetDate() const
{
    VerifyAccess();
    return m_Date;
}

bool DateEdit::Commit()
{
    VerifyStructuralMutation();
    if (m_Syncing)
    {
        return true;
    }
    const auto text = m_Editor->GetText();
    const auto date = CalendarDate::Parse(text);
    if (!text.empty() && !date)
    {
        return false;
    }
    SetDate(date);
    return true;
}

TextBox& DateEdit::GetEditor() const
{
    VerifyAccess();
    return *m_Editor;
}

void DateEdit::ShowCalendar()
{
    VerifyStructuralMutation();
    if (!GetContext())
    {
        throw std::logic_error("Attach DateEdit before opening its calendar.");
    }
    auto* popup = dynamic_cast<Popup*>(m_Popup.Get());
    if (popup && popup->GetContext() != GetContext())
    {
        popup = nullptr;
    }
    if (!popup)
    {
        popup = &Popup::Create(*this);
        popup->AddClass("calendar-popup");
        popup->SetWidth(SizeRule::Fixed(300));
        popup->SetHeight(SizeRule::Fixed(284));
        auto& calendar = popup->Add<Calendar>();
        calendar.SetWidth(SizeRule::Fill());
        calendar.SetHeight(SizeRule::Fill());
        const WidgetRef self(this), surface(popup);
        calendar.Activated().Connect(
            [self, surface](CalendarDate date)
            {
                if (auto* menu = dynamic_cast<Popup*>(surface.Get()))
                {
                    menu->Close({PopupCloseReason::Accepted, 0});
                }
                if (auto* live = dynamic_cast<DateEdit*>(self.Get()))
                {
                    live->SetDate(date);
                }
            });
        m_Popup = WidgetRef(popup);
    }
    auto& calendar = static_cast<Calendar&>(*popup->GetChildren().front());
    calendar.SetDate(m_Date.value_or(CalendarDate{}));
    popup->Open(*this);
}

Signal<std::optional<CalendarDate>>& DateEdit::DateChanged()
{
    VerifyAccess();
    return m_DateChanged;
}
} // namespace EStudioUI
