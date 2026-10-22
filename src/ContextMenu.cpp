#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/UiContext.h>

#include <stdexcept>

namespace EStudioUI
{
ContextMenu::ContextMenu() : Widget("contextmenu")
{
    SetVisible(false);
}

ToolButton& ContextMenu::AddAction(std::string text, bool enabled)
{
    VerifyStructuralMutation();
    auto& action = Add<ToolButton>(std::move(text));
    action.AddClass("menu-action");
    action.SetEnabled(enabled);
    const WidgetRef menu(this);
    action.Clicked().Connect(
        [menu]
        {
            if (auto* live = dynamic_cast<ContextMenu*>(menu.Get()))
                live->Close();
        });
    return action;
}

void ContextMenu::AddSeparator()
{
    VerifyStructuralMutation();
    auto& separator = Add<Widget>("separator");
    separator.AddClass("menu-separator");
}

void ContextMenu::Open(Point position)
{
    VerifyAccess();
    if (!GetContext())
        throw std::logic_error("Attach a context menu to the UI root before opening it.");
    GetContext()->ShowPopup(*this, position);
}

void ContextMenu::Close()
{
    VerifyAccess();
    if (IsOpen())
        GetContext()->ClosePopup();
}

bool ContextMenu::IsOpen() const
{
    VerifyAccess();
    return GetContext() && GetContext()->GetPopup() == this;
}

bool ContextMenu::IsFocusable() const
{
    VerifyAccess();
    return true;
}

bool ContextMenu::OnKeyPreview(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Control || modifiers.m_Alt ||
        (key != Key::Down && key != Key::Up && key != Key::Home && key != Key::End && key != Key::Enter))
        return false;
    std::vector<Widget*> actions;
    for (const auto& child : GetChildren())
        if (dynamic_cast<ToolButton*>(child.get()) && child->IsEnabled() && child->IsVisible())
            actions.push_back(child.get());
    if (actions.empty())
        return true;
    const auto found = std::find(actions.begin(), actions.end(), GetContext()->GetFocus());
    if (key == Key::Enter && found != actions.end())
        return false;
    const auto count = static_cast<std::ptrdiff_t>(actions.size());
    std::ptrdiff_t index = found == actions.end() ? (key == Key::Up ? 0 : -1) : found - actions.begin();
    index = key == Key::Home ? 0 : key == Key::End ? count - 1 : (index + (key == Key::Up ? -1 : 1) + count) % count;
    auto* action = actions[static_cast<std::size_t>(index)];
    auto* context = GetContext();
    context->SetFocus(action);
    if (key == Key::Enter)
    {
        context->KeyEvent(Key::Enter, true);
        return true;
    }
    return true;
}
} // namespace EStudioUI
