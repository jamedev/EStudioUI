#include <EStudioUI/ContextMenu.h>
#include <EStudioUI/UiContext.h>

#include <stdexcept>

namespace EStudioUI
{
ContextMenu::ContextMenu() : Widget("contextmenu")
{
    SetVisible(false);
}

ContextMenu::~ContextMenu() = default;

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
                live->CloseMenuChain();
        });
    return action;
}

ToolButton& ContextMenu::AddAction(std::shared_ptr<Action> action)
{
    if (!action)
        throw std::invalid_argument("A menu action cannot be null.");
    (void)action->GetSnapshot();
    auto& button = AddAction(std::string{});
    button.SetAction(std::move(action));
    return button;
}

ContextMenu& ContextMenu::AddMenu(std::string text)
{
    VerifyStructuralMutation();
    auto menu = std::make_unique<ContextMenu>();
    auto& result = *menu;
    auto& button = Add<ToolButton>(std::move(text));
    button.AddClass("menu-action");
    button.AddClass("submenu-action");
    const auto index = m_Submenus.size();
    m_Submenus.push_back({WidgetRef(&button), std::move(menu), {}});
    const WidgetRef owner(this);
    button.Clicked().Connect(
        [owner, index]
        {
            if (auto* live = dynamic_cast<ContextMenu*>(owner.Get()))
                live->OpenSubmenu(index);
        });
    return result;
}

void ContextMenu::OpenSubmenu(std::size_t index)
{
    if (!IsOpen() || index >= m_Submenus.size())
        return;
    auto& entry = m_Submenus[index];
    auto* button = entry.m_Button.Get();
    if (!button || !button->IsEnabled())
        return;
    auto* context = GetContext();
    if (entry.m_Pending)
        entry.m_Menu = WidgetRef(&context->AddOwnedPopup(std::move(entry.m_Pending), *button));
    if (auto* menu = dynamic_cast<ContextMenu*>(entry.m_Menu.Get()))
    {
        const auto bounds = button->GetBounds();
        menu->Open(context->ToWindowPoint(*button, {bounds.m_X + bounds.m_Width, bounds.m_Y}), this);
    }
}

void ContextMenu::AddSeparator()
{
    VerifyStructuralMutation();
    auto& separator = Add<Widget>("separator");
    separator.AddClass("menu-separator");
}

PopupSession ContextMenu::Open(Point position, Widget* parentPopup)
{
    VerifyAccess();
    if (!GetContext())
        throw std::logic_error("Attach a context menu to the UI root before opening it.");
    PopupOptions options;
    options.m_Parent = parentPopup;
    return GetContext()->ShowPopup(*this, position, options);
}

void ContextMenu::Close()
{
    VerifyAccess();
    if (IsOpen())
        GetContext()->ClosePopup(*this);
}

bool ContextMenu::IsOpen() const
{
    VerifyAccess();
    return GetContext() && GetContext()->IsPopupOpen(*this);
}

void ContextMenu::CloseMenuChain()
{
    auto* context = GetContext();
    if (!context || !IsOpen())
        return;
    ContextMenu* first = this;
    while (auto* parent = dynamic_cast<ContextMenu*>(context->GetPopupParent(*first)))
        first = parent;
    context->ClosePopup(*first);
}

bool ContextMenu::IsFocusable() const
{
    VerifyAccess();
    return true;
}

bool ContextMenu::OnKeyPreview(Key key, bool down, bool, Modifiers modifiers)
{
    if (down && key == Key::Left && GetContext()->GetPopupParent(*this))
    {
        Close();
        return true;
    }
    if (down && key == Key::Right)
        for (std::size_t i = 0; i < m_Submenus.size(); ++i)
            if (m_Submenus[i].m_Button.Get() == GetContext()->GetFocus())
            {
                OpenSubmenu(i);
                return true;
            }
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
    const WidgetRef action(actions[static_cast<std::size_t>(index)]);
    auto* context = GetContext();
    context->SetFocus(action.Get());
    if (key == Key::Enter)
    {
        if (auto* button = dynamic_cast<Button*>(action.Get()))
            button->Trigger();
        return true;
    }
    return true;
}
} // namespace EStudioUI
