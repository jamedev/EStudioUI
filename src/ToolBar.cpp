#include <EStudioUI/ToolBar.h>
#include <EStudioUI/UiContext.h>

namespace EStudioUI
{
ToolBar::ToolBar() : Widget("toolbar")
{
    SetLayoutDirection(Axis::Horizontal);
}

ToolButton& ToolBar::AddButton(std::string text)
{
    VerifyStructuralMutation();
    auto& button = Add<ToolButton>(std::move(text));
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
    return Add<SplitToolButton>(std::move(text));
}

Widget& ToolBar::AddSeparator()
{
    VerifyStructuralMutation();
    auto& separator = Add<Widget>("separator");
    separator.AddClass("toolbar-separator");
    separator.SetHeight(SizeRule::Fill());
    return separator;
}

bool ToolBar::OnKeyPreview(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Control || modifiers.m_Alt || (key != Key::Left && key != Key::Right))
        return false;
    std::vector<Widget*> buttons;
    for (auto& child : GetChildren())
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
