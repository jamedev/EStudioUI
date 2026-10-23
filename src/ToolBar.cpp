#include <EStudioUI/ToolBar.h>
#include <EStudioUI/UiContext.h>

#include "OverflowStrip.h"

namespace EStudioUI
{
ToolBar::ToolBar() : Widget("toolbar")
{
    SetLayoutDirection(Axis::Horizontal);
    SetHeight(SizeRule::Content());
    auto& backward = Widget::Add<ToolButton>("‹");
    backward.AddClass("toolbar-scroll");
    backward.SetAccessibleName("Scroll toolbar left");
    backward.SetWidth(SizeRule::Fixed(24));
    backward.SetHeight(SizeRule::Fill());
    m_Content = &Widget::Add<Detail::OverflowStrip>("toolbar-strip");
    auto& forward = Widget::Add<ToolButton>("›");
    forward.AddClass("toolbar-scroll");
    forward.SetAccessibleName("Scroll toolbar right");
    forward.SetWidth(SizeRule::Fixed(24));
    forward.SetHeight(SizeRule::Fill());
    const WidgetRef strip(m_Content);
    backward.Clicked().Connect(
        [strip]
        {
            if (auto* live = dynamic_cast<Detail::OverflowStrip*>(strip.Get()))
                live->ScrollPage(-1);
        });
    forward.Clicked().Connect(
        [strip]
        {
            if (auto* live = dynamic_cast<Detail::OverflowStrip*>(strip.Get()))
                live->ScrollPage(1);
        });
}

Widget& ToolBar::AddChild(std::unique_ptr<Widget> child)
{
    VerifyStructuralMutation();
    return Content().AddChild(std::move(child));
}

bool ToolBar::RemoveChild(Widget& child)
{
    VerifyStructuralMutation();
    return Content().RemoveChild(child);
}

Widget& ToolBar::Content() const
{
    VerifyAccess();
    return *m_Content;
}

void ToolBar::ScrollBy(float distance)
{
    VerifyStructuralMutation();
    if (!std::isfinite(distance))
        throw std::invalid_argument("Toolbar scroll distance must be finite.");
    // Wheel units preserve the strip's existing bounded, prepared-offset path.
    static_cast<Detail::OverflowStrip*>(m_Content)->ScrollWheel({-distance / 48, 0});
}

bool ToolBar::OnScroll(Point delta, Modifiers)
{
    return static_cast<Detail::OverflowStrip*>(m_Content)->ScrollWheel(delta);
}

ToolButton& ToolBar::AddButton(std::string text)
{
    VerifyStructuralMutation();
    auto& button = Content().Add<ToolButton>(std::move(text));
    button.AddClass("toolbar-button");
    return button;
}

ToolButton& ToolBar::AddAction(std::shared_ptr<Action> action)
{
    if (!action)
        throw std::invalid_argument("A toolbar action cannot be null.");
    (void)action->GetSnapshot();
    auto& button = AddButton({});
    button.SetAction(std::move(action));
    return button;
}

SplitToolButton& ToolBar::AddSplitButton(std::string text)
{
    VerifyStructuralMutation();
    return Content().Add<SplitToolButton>(std::move(text));
}

Widget& ToolBar::AddSeparator()
{
    VerifyStructuralMutation();
    auto& separator = Content().Add<Widget>("separator");
    separator.AddClass("toolbar-separator");
    separator.SetHeight(SizeRule::Fill());
    return separator;
}

bool ToolBar::OnKeyPreview(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Control || modifiers.m_Alt || (key != Key::Left && key != Key::Right))
        return false;
    std::vector<Widget*> buttons;
    for (auto& child : Content().GetChildren())
    {
        if (!child->IsEnabled() || !child->IsVisible())
            continue;
        if (dynamic_cast<ToolButton*>(child.get()))
            buttons.push_back(child.get());
        else if (dynamic_cast<SplitToolButton*>(child.get()))
            for (const auto& part : child->GetChildren())
                if (dynamic_cast<ToolButton*>(part.get()) && part->IsEnabled() && part->IsVisible())
                    buttons.push_back(part.get());
    }
    const auto current = std::find(buttons.begin(), buttons.end(), GetContext()->GetFocus());
    if (current == buttons.end() || buttons.empty())
        return false;
    const auto count = static_cast<std::ptrdiff_t>(buttons.size());
    const auto next = (current - buttons.begin() + (key == Key::Right ? 1 : -1) + count) % count;
    GetContext()->SetFocus(buttons[static_cast<std::size_t>(next)]);
    return true;
}
} // namespace EStudioUI
