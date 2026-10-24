#include "PickerButton.h"
#include <EStudioUI/TimeEdit.h>
#include "PropertyNotification.h"
#include <EStudioUI/Popup.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
class TimeValidator final : public InputValidator
{
  public:
    ValidationState Validate(std::string_view text) const override
    {
        if (text.empty() || ClockTime::Parse(text))
        {
            return ValidationState::Acceptable;
        }
        if (text.size() > 12)
        {
            return ValidationState::Invalid;
        }
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (i == 2 || i == 5 ? text[i] != ':' : i == 8 ? text[i] != '.' : text[i] < '0' || text[i] > '9')
            {
                return ValidationState::Invalid;
            }
        }
        return ValidationState::Intermediate;
    }
};
} // namespace

TimeEdit::TimeEdit() : Widget("timeedit")
{
    SetLayoutDirection(Axis::Horizontal);
    SetGap(0);
    m_Editor = &Add<TextBox>();
    m_Editor->SetWidth(SizeRule::Fill());
    m_Editor->SetPlaceholder("HH:MM:SS");
    m_Editor->SetMaxBytes(12);
    m_Editor->SetValidator(std::make_shared<TimeValidator>());
    const WidgetRef self(this);
    auto& button = Add<Detail::PickerButton>(Detail::PickerButton::Symbol::Clock);
    button.SetAccessibleName("Choose time");
    button.SetWidth(SizeRule::Fixed(32));
    button.Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<TimeEdit*>(self.Get()))
            {
                live->ShowPicker();
            }
        });
    m_Editor->Submitted().Connect(
        [self](const std::string&)
        {
            if (auto* live = dynamic_cast<TimeEdit*>(self.Get()))
            {
                live->Commit();
            }
        });
    m_Editor->FocusLost().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<TimeEdit*>(self.Get()))
            {
                live->Commit();
            }
        });
}

void TimeEdit::SetTime(std::optional<ClockTime> time)
{
    VerifyStructuralMutation();
    if (time && !time->IsValid())
    {
        throw std::invalid_argument("TimeEdit requires a valid local clock value.");
    }
    const bool changed = time != m_Time;
    m_Time = time;
    const WidgetRef self(this);
    {
        Detail::PropertySyncScope scope(*this, m_Syncing);
        m_Editor->SetText(time ? time->ToString() : "");
    }
    if (auto* live = dynamic_cast<TimeEdit*>(self.Get()); live && changed && live->m_Time == time)
    {
        Detail::EmitPropertyNotification(*live, live->m_NotificationDepth, live->m_TimeChanged, time);
    }
}

std::optional<ClockTime> TimeEdit::GetTime() const
{
    VerifyAccess();
    return m_Time;
}

bool TimeEdit::Commit()
{
    VerifyStructuralMutation();
    if (m_Syncing)
    {
        return true;
    }
    const auto text = m_Editor->GetText();
    const auto time = ClockTime::Parse(text);
    if (!text.empty() && !time)
    {
        return false;
    }
    SetTime(time);
    return true;
}

TextBox& TimeEdit::GetEditor() const
{
    VerifyAccess();
    return *m_Editor;
}

void TimeEdit::ShowPicker()
{
    VerifyStructuralMutation();
    if (!GetContext())
    {
        throw std::logic_error("Attach the time editor before showing its picker.");
    }
    auto* popup = dynamic_cast<Popup*>(m_Popup.Get());
    if (popup && popup->GetContext() != GetContext())
    {
        popup = nullptr;
    }
    if (!popup)
    {
        popup = &Popup::Create(*this);
        popup->AddClass("time-picker-popup");
        popup->SetWidth(SizeRule::Fixed(320));
        for (int i = 0; i < 4; ++i)
        {
            auto& field = popup->Add<Widget>();
            field.SetWidth(SizeRule::Fill());
            field.SetLayoutDirection(Axis::Horizontal);
            const auto caption = i == 0 ? "Hour" : i == 1 ? "Minute" : i == 2 ? "Second" : "Millisecond";
            auto& label = field.Add<Label>(caption);
            label.SetWidth(SizeRule::Fixed(100));
            auto& input = field.Add<NumericInput>();
            input.SetDecimals(0);
            input.SetRange(0, i == 0 ? 23 : i == 3 ? 999 : 59);
            input.SetWidth(SizeRule::Fill());
            input.SetAccessibleName(i == 0 ? "Hour" : i == 1 ? "Minute" : i == 2 ? "Second" : "Millisecond");
        }
        auto& row = popup->Add<Widget>();
        row.SetLayoutDirection(Axis::Horizontal);
        auto& apply = row.Add<ToolButton>("Apply");
        auto& cancel = row.Add<ToolButton>("Cancel");
        const WidgetRef self(this), surface(popup);
        apply.Clicked().Connect(
            [self, surface]
            {
                auto* menu = dynamic_cast<Popup*>(surface.Get());
                if (!menu)
                {
                    return;
                }
                std::array<unsigned, 4> values{};
                for (std::size_t i = 0; i < 4; ++i)
                {
                    auto& input = static_cast<NumericInput&>(*menu->GetChildren()[i]->GetChildren()[1]);
                    if (!input.Commit())
                    {
                        return;
                    }
                    values[i] = static_cast<unsigned>(std::round(input.GetValue()));
                }
                const ClockTime value{values[0], values[1], values[2], values[3]};
                menu->Close({PopupCloseReason::Accepted, 0});
                if (auto* live = dynamic_cast<TimeEdit*>(self.Get()))
                {
                    live->SetTime(value);
                }
            });
        cancel.Clicked().Connect(
            [surface]
            {
                if (auto* menu = dynamic_cast<Popup*>(surface.Get()))
                {
                    menu->Close({PopupCloseReason::Rejected, 0});
                }
            });
        m_Popup = WidgetRef(popup);
    }
    const auto time = m_Time.value_or(ClockTime{});
    const std::array<unsigned, 4> values{time.m_Hour, time.m_Minute, time.m_Second, time.m_Millisecond};
    for (std::size_t i = 0; i < 4; ++i)
    {
        static_cast<NumericInput&>(*popup->GetChildren()[i]->GetChildren()[1]).SetValue(values[i]);
    }
    popup->Open(*this);
}

Signal<std::optional<ClockTime>>& TimeEdit::TimeChanged()
{
    VerifyAccess();
    return m_TimeChanged;
}

bool TimeEdit::OnKeyPreview(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Control || modifiers.m_Alt || (key != Key::Up && key != Key::Down))
    {
        return false;
    }
    const auto current = m_Time.value_or(ClockTime{});
    const auto seconds = std::clamp(static_cast<int>(current.m_Hour * 3600 + current.m_Minute * 60 + current.m_Second) +
                                        (key == Key::Up ? 1 : -1),
                                    0, 86399);
    SetTime(ClockTime{static_cast<unsigned>(seconds / 3600), static_cast<unsigned>(seconds / 60 % 60),
                      static_cast<unsigned>(seconds % 60), current.m_Millisecond});
    return true;
}
} // namespace EStudioUI
