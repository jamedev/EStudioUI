#include <EStudioUI/Calendar.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>
#include <chrono>
#include <algorithm>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
Style TextStyle(const Style& owner)
{
    Style style;
    style.m_Color = owner.m_Color;
    style.m_FontSize = owner.m_FontSize;
    style.m_FontFamily = owner.m_FontFamily;
    style.m_FontWeight = owner.m_FontWeight;
    style.m_FontStyle = owner.m_FontStyle;
    style.m_TextDirection = owner.m_TextDirection;
    style.m_Language = owner.m_Language;
    return style;
}
} // namespace

Calendar::Calendar() : Widget("calendar")
{
    RegisterStylePart("day", Selected | Focus,
                      [](const Style& owner, unsigned state)
                      {
                          auto style = TextStyle(owner);
                          style.m_Background = state & Selected ? owner.m_SelectionBackground : Color{0, 0, 0, 0};
                          style.m_Color = state & Selected ? owner.m_SelectionColor : owner.m_Color;
                          style.m_Radius = 4;
                          style.m_BorderWidth = (state & (Selected | Focus)) == (Selected | Focus) ? 1.0f : 0.0f;
                          return style;
                      });
    RegisterStylePart("header", 0,
                      [](const Style& owner, unsigned)
                      {
                          auto style = TextStyle(owner);
                          style.m_BorderWidth = 0;
                          style.m_Background = {0, 0, 0, 0};
                          return style;
                      });
}

void Calendar::SetDate(CalendarDate date)
{
    VerifyStructuralMutation();
    if (!date.IsValid())
    {
        throw std::invalid_argument("Calendar requires a valid Gregorian date.");
    }
    const bool changed = date != m_Date;
    if (m_Month.m_Year != date.m_Year || m_Month.m_Month != date.m_Month)
    {
        m_Month = {date.m_Year, date.m_Month, 1};
        ++m_Revision;
    }
    m_Date = date;
    RequestRepaint();
    NotifySemanticsChanged();
    if (changed)
    {
        const auto lease = RetainOwner();
        m_DateChanged.Emit(date);
    }
}

CalendarDate Calendar::GetDate() const
{
    VerifyAccess();
    return m_Date;
}

void Calendar::MoveMonth(int delta)
{
    VerifyStructuralMutation();
    const auto index = static_cast<long long>(m_Month.m_Year - 1) * 12 + m_Month.m_Month - 1;
    const auto next = std::clamp(index + delta, 0LL, 119987LL);
    if (next == index)
    {
        return;
    }
    m_Month = {static_cast<int>(next / 12) + 1, static_cast<unsigned>(next % 12) + 1, 1};
    ++m_Revision;
    NotifySemanticsChanged();
    RequestRepaint();
}

Signal<CalendarDate>& Calendar::DateChanged()
{
    VerifyAccess();
    return m_DateChanged;
}

Signal<CalendarDate>& Calendar::Activated()
{
    VerifyAccess();
    return m_Activated;
}

bool Calendar::IsFocusable() const
{
    VerifyAccess();
    return true;
}

Point Calendar::Measure(FontFace&) const
{
    return {280, 264};
}

unsigned Calendar::FirstColumn() const
{
    const std::chrono::year_month_day first{std::chrono::year{m_Month.m_Year}, std::chrono::month{m_Month.m_Month},
                                            std::chrono::day{1}};
    return std::chrono::weekday{std::chrono::sys_days{first}}.iso_encoding() - 1;
}

unsigned Calendar::DayCount() const
{
    return static_cast<unsigned>(std::chrono::year_month_day_last{
        std::chrono::year{m_Month.m_Year}, std::chrono::month_day_last{std::chrono::month{m_Month.m_Month}}}
                                     .day());
}

Rect Calendar::Cell(unsigned day) const
{
    const auto b = GetContentBounds();
    const auto width = b.m_Width / 7, height = b.m_Height / 8;
    if (day == 0)
    {
        return {b.m_X, b.m_Y, width, height};
    }
    if (day == 32)
    {
        return {b.m_X + 6 * width, b.m_Y, width, height};
    }
    const auto index = FirstColumn() + day - 1;
    return {b.m_X + static_cast<float>(index % 7) * width, b.m_Y + (2 + static_cast<float>(index / 7)) * height, width,
            height};
}

void Calendar::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    const auto b = GetContentBounds();
    const auto clip = GetClip().Intersect(b);
    auto caption = [&](std::size_t index, std::string text, Rect bounds, const Style& style)
    {
        auto& layout = m_Text[index];
        if (m_Captions[index] != text || !font.Matches(layout, style))
        {
            m_Captions[index] = std::move(text);
            layout = font.Shape(m_Captions[index], style);
        }
        auto color = style.m_Color;
        color.m_A *= opacity * style.m_Opacity;
        renderer.DrawText(
            layout,
            {bounds.m_X + (bounds.m_Width - layout.m_Width) * 0.5f,
             bounds.m_Y + (bounds.m_Height - layout.m_Ascender - layout.m_Descender) * 0.5f + layout.m_Ascender},
            color, clip);
    };
    const auto& header = GetPartStyle("header");
    caption(0, "‹", Cell(0), header);
    caption(40, "›", Cell(32), header);
    caption(41, std::to_string(m_Month.m_Year) + " / " + std::to_string(m_Month.m_Month),
            {b.m_X + b.m_Width / 7, b.m_Y, b.m_Width * 5 / 7, b.m_Height / 8}, header);
    const std::array<const char*, 7> weekdays{"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    for (unsigned i = 0; i < 7; ++i)
    {
        caption(i + 1, weekdays[i],
                {b.m_X + static_cast<float>(i) * b.m_Width / 7, b.m_Y + b.m_Height / 8, b.m_Width / 7, b.m_Height / 8},
                header);
    }
    for (unsigned day = 1; day <= DayCount(); ++day)
    {
        const bool selected = m_Date == CalendarDate{m_Month.m_Year, m_Month.m_Month, day};
        const auto& style = GetPartStyle("day", selected ? Selected | (IsFocused() ? Focus : 0u) : 0u);
        const auto bounds = Cell(day);
        if (!style.m_Displayed)
        {
            continue;
        }
        renderer.DrawBox(bounds, style, clip, opacity * style.m_Opacity);
        caption(day + 8, std::to_string(day), bounds, style);
    }
}

void Calendar::OnPointerDown(const PointerEvent& event)
{
    if (Cell(0).Contains(event.m_Position))
    {
        MoveMonth(-1);
        return;
    }
    if (Cell(32).Contains(event.m_Position))
    {
        MoveMonth(1);
        return;
    }
    for (unsigned day = 1; day <= DayCount(); ++day)
    {
        if (Cell(day).Contains(event.m_Position))
        {
            const CalendarDate date{m_Month.m_Year, m_Month.m_Month, day};
            const WidgetRef self(this);
            SetDate(date);
            if (auto* live = dynamic_cast<Calendar*>(self.Get()))
            {
                live->m_Activated.Emit(date);
            }
            return;
        }
    }
}

void Calendar::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Control || modifiers.m_Alt)
    {
        return;
    }
    if (key == Key::Enter || key == Key::Space)
    {
        const auto date = m_Date;
        m_Activated.Emit(date);
        return;
    }
    if (key == Key::PageUp || key == Key::PageDown)
    {
        MoveMonth(key == Key::PageUp ? -1 : 1);
        SetDate({m_Month.m_Year, m_Month.m_Month, std::min(m_Date.m_Day, DayCount())});
        return;
    }
    const int delta = key == Key::Left ? -1 : key == Key::Right ? 1 : key == Key::Up ? -7 : key == Key::Down ? 7 : 0;
    if (!delta)
    {
        return;
    }
    const auto days = std::chrono::sys_days{std::chrono::year{m_Date.m_Year} / m_Date.m_Month / m_Date.m_Day} +
                      std::chrono::days{delta};
    const std::chrono::year_month_day next{days};
    CalendarDate date{static_cast<int>(next.year()), static_cast<unsigned>(next.month()),
                      static_cast<unsigned>(next.day())};
    if (date.IsValid())
    {
        SetDate(date);
    }
}

SemanticInfo Calendar::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Role = SemanticRole::List;
    info.m_SelectionRequired = true;
    info.m_Value = m_Date.ToString();
    info.m_StructureRevision = m_Revision;
    return info;
}

std::size_t Calendar::GetSemanticChildCount(SemanticKey parent) const
{
    return parent == SemanticKey{} ? DayCount() + 2 : 0;
}

std::optional<SemanticKey> Calendar::GetSemanticChildAt(SemanticKey parent, std::size_t index) const
{
    if (parent != SemanticKey{} || index >= DayCount() + 2)
    {
        return {};
    }
    if (index < 2)
    {
        return SemanticKey{index + 1, 1, 0};
    }
    return SemanticKey{static_cast<std::uint64_t>(m_Month.m_Year) * 10000 + m_Month.m_Month * 100 + index - 1, 0, 0};
}

std::optional<std::size_t> Calendar::GetSemanticChildIndex(SemanticKey key) const
{
    if (key.m_Generation != 0)
    {
        return {};
    }
    if (key.m_Part == 1 && key.m_Item >= 1 && key.m_Item <= 2)
    {
        return key.m_Item - 1;
    }
    const auto first = static_cast<std::uint64_t>(m_Month.m_Year) * 10000 + m_Month.m_Month * 100;
    if (key.m_Part != 0 || key.m_Item <= first || key.m_Item > first + DayCount())
    {
        return {};
    }
    return static_cast<std::size_t>(key.m_Item - first + 1);
}

std::optional<SemanticVirtualNode> Calendar::DescribeSemanticChild(SemanticKey key) const
{
    const auto index = GetSemanticChildIndex(key);
    if (!index)
    {
        return {};
    }
    SemanticVirtualNode node;
    if (*index < 2)
    {
        node.m_Info.m_Role = SemanticRole::Button;
        node.m_Info.m_Name = *index == 0 ? "Previous month" : "Next month";
        node.m_Info.m_Actions = SemanticAction::Invoke;
        node.m_Bounds = Cell(*index == 0 ? 0 : 32);
    }
    else
    {
        const CalendarDate date{m_Month.m_Year, m_Month.m_Month, static_cast<unsigned>(*index - 1)};
        node.m_Info.m_Role = SemanticRole::ListItem;
        node.m_Info.m_Name = date.ToString();
        node.m_Info.m_Selected = date == m_Date;
        node.m_Info.m_Actions = SemanticAction::Select | SemanticAction::Invoke;
        node.m_Bounds = Cell(date.m_Day);
    }
    return node;
}

std::optional<SemanticKey> Calendar::GetSemanticFocus() const
{
    if (m_Date.m_Year != m_Month.m_Year || m_Date.m_Month != m_Month.m_Month)
    {
        return {};
    }
    return SemanticKey{static_cast<std::uint64_t>(m_Date.m_Year) * 10000 + m_Date.m_Month * 100 + m_Date.m_Day, 0, 0};
}

std::size_t Calendar::GetSemanticSelectionCount() const
{
    return GetSemanticFocus() ? 1 : 0;
}

std::vector<SemanticKey> Calendar::GetSemanticSelection(std::size_t offset, std::size_t limit) const
{
    const auto key = GetSemanticFocus();
    return key && offset == 0 && limit > 0 ? std::vector<SemanticKey>{*key} : std::vector<SemanticKey>{};
}

SemanticStatus Calendar::OnSemanticChildAction(SemanticKey key, const SemanticCommand& command)
{
    const auto index = GetSemanticChildIndex(key);
    if (!index)
    {
        return SemanticStatus::Unavailable;
    }
    if (*index < 2)
    {
        if (command.m_Action != SemanticAction::Invoke)
        {
            return SemanticStatus::Unsupported;
        }
        MoveMonth(*index == 0 ? -1 : 1);
    }
    else
    {
        if (command.m_Action != SemanticAction::Invoke && command.m_Action != SemanticAction::Select)
        {
            return SemanticStatus::Unsupported;
        }
        const CalendarDate date{m_Month.m_Year, m_Month.m_Month, static_cast<unsigned>(*index - 1)};
        const WidgetRef self(this);
        SetDate(date);
        if (command.m_Action == SemanticAction::Invoke)
        {
            if (auto* live = dynamic_cast<Calendar*>(self.Get()))
            {
                live->m_Activated.Emit(date);
            }
        }
    }
    return SemanticStatus::Succeeded;
}
} // namespace EStudioUI
