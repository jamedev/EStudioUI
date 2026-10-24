#include "PickerButton.h"
#include <EStudioUI/NotificationCenter.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
NotificationCenter::NotificationCenter() : Widget("notificationcenter")
{
    SetWidth(SizeRule::Fixed(340));
    SetHeight(SizeRule::Content());
    SetGap(8);
    SetVisible(false);
}

NotificationCenter& NotificationCenter::Create(UiContext& context)
{
    return static_cast<NotificationCenter&>(context.AddOverlay(std::make_unique<NotificationCenter>()));
}

std::uint64_t NotificationCenter::Show(std::string text, FeedbackSeverity severity, double seconds)
{
    VerifyStructuralMutation();
    if (!GetContext())
    {
        throw std::logic_error("Attach notifications before showing a message.");
    }
    if (text.size() > 16384 || !std::isfinite(seconds) || seconds < 0 || seconds > 86400 ||
        severity < FeedbackSeverity::Information || severity > FeedbackSeverity::Error)
    {
        throw std::invalid_argument("Notifications accept up to 16 KiB and a timeout from zero through one day.");
    }
    if (m_Entries.size() == 4)
    {
        Dismiss(m_Entries.front().m_Id);
    }
    const auto id = UiDispatcher::AllocateTargetKey();
    auto& row = Add<Widget>("notification");
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetWidth(SizeRule::Fill());
    row.SetHeight(SizeRule::Content());
    auto& message = row.Add<ValidationMessage>();
    message.SetWidth(SizeRule::Fill());
    message.SetMessage(std::move(text), severity);
    auto& close = row.Add<Detail::PickerButton>(Detail::PickerButton::Symbol::Close);
    close.SetAccessibleName("Dismiss notification");
    close.SetWidth(SizeRule::Fixed(28));
    const WidgetRef self(this);
    close.Clicked().Connect(
        [self, id]
        {
            if (auto* live = dynamic_cast<NotificationCenter*>(self.Get()))
            {
                live->Dismiss(id);
            }
        });
    UiTimer timer;
    if (seconds > 0)
    {
        timer = GetContext()->After(*this, seconds, [id](NotificationCenter& live) { live.Dismiss(id); });
    }
    m_Entries.push_back({id, WidgetRef(&row), std::move(timer)});
    SetVisible(true);
    return id;
}

bool NotificationCenter::Dismiss(std::uint64_t id)
{
    VerifyStructuralMutation();
    const auto found =
        std::find_if(m_Entries.begin(), m_Entries.end(), [id](const auto& entry) { return entry.m_Id == id; });
    if (found == m_Entries.end())
    {
        return false;
    }
    WidgetRef row = found->m_Row;
    m_Entries.erase(found);
    if (row.Get())
    {
        RemoveChild(*row.Get());
    }
    SetVisible(!m_Entries.empty());
    return true;
}

void NotificationCenter::Clear()
{
    VerifyStructuralMutation();
    while (!m_Entries.empty())
    {
        Dismiss(m_Entries.back().m_Id);
    }
}

SemanticInfo NotificationCenter::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Name = "Notifications";
    info.m_Live = SemanticLiveSetting::Polite;
    for (const auto& entry : m_Entries)
    {
        if (auto* row = entry.m_Row.Get())
        {
            if (const auto* message = dynamic_cast<ValidationMessage*>(row->GetChildren().front().get()))
            {
                if (!info.m_Value.empty())
                {
                    info.m_Value += "\n";
                }
                info.m_Value += message->GetText();
                if (message->GetSeverity() == FeedbackSeverity::Error)
                {
                    info.m_Live = SemanticLiveSetting::Assertive;
                }
            }
        }
    }
    return info;
}

std::size_t NotificationCenter::GetCount() const
{
    VerifyAccess();
    return m_Entries.size();
}
} // namespace EStudioUI
