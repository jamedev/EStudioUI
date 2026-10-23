#include <EStudioUI/MenuBar.h>
#include <EStudioUI/UiContext.h>

namespace EStudioUI
{
namespace
{
class MenuTrigger final : public Button
{
  public:
    MenuTrigger(std::string title, WidgetRef bar, int index)
        : Button(std::move(title), "menu-trigger"), m_Bar(bar), m_Index(index)
    {
        SetAccessibleRole(SemanticRole::MenuItem);
        SetWidth(SizeRule::Content());
    }

    bool IsFocusable() const override
    {
        VerifyAccess();
        return false;
    }

  protected:
    SemanticInfo DescribeSemantics() const override
    {
        auto info = Button::DescribeSemantics();
        info.m_Role = SemanticRole::MenuItem;
        info.m_Expandable = true;
        const auto* bar = dynamic_cast<MenuBar*>(m_Bar.Get());
        info.m_Expanded = bar && bar->GetCurrentIndex() == m_Index;
        info.m_Actions = SemanticAction::Invoke | SemanticAction::Expand | SemanticAction::Collapse;
        return info;
    }

    SemanticStatus OnSemanticAction(const SemanticCommand& command) override
    {
        if (auto* bar = dynamic_cast<MenuBar*>(m_Bar.Get()))
        {
            if (command.m_Action == SemanticAction::Expand)
            {
                bar->OpenMenu(m_Index);
                return SemanticStatus::Succeeded;
            }
            if (command.m_Action == SemanticAction::Collapse)
            {
                if (bar->GetCurrentIndex() == m_Index)
                    bar->CloseMenu();
                return SemanticStatus::Succeeded;
            }
        }
        return Button::OnSemanticAction(command);
    }

  private:
    WidgetRef m_Bar;
    int m_Index;
};
} // namespace

MenuBar::MenuBar() : Widget("menubar")
{
    SetLayoutDirection(Axis::Horizontal);
    SetHeight(SizeRule::Content());
    SetGap(3);
}

MenuBar::~MenuBar()
{
    m_Retiring = true;
    m_Menus.clear();
}

void MenuBar::TrackCommand(const std::shared_ptr<Action>& action, bool add)
{
    if (!action || m_Retiring)
        return;
    if (add)
    {
        auto [entry, inserted] = m_Commands.try_emplace(action.get(), action, 0);
        if (inserted)
            Widget::AddAction(action);
        ++entry->second.second;
    }
    else if (const auto found = m_Commands.find(action.get()); found != m_Commands.end())
    {
        if (--found->second.second == 0)
        {
            Widget::RemoveAction(action);
            m_Commands.erase(found);
        }
    }
}

ContextMenu& MenuBar::AddMenu(std::string title)
{
    VerifyStructuralMutation();
    if (m_Menus.size() >= 128)
        throw std::length_error("A menu bar is limited to 128 top-level menus.");
    auto menu = std::make_unique<ContextMenu>();
    auto& result = *menu;
    const auto index = static_cast<int>(m_Menus.size());
    auto& button = Add<MenuTrigger>(std::move(title), WidgetRef(this), index);
    m_Menus.push_back({WidgetRef(&button), std::move(menu), {}});
    const WidgetRef self(this);
    result.m_MenuBar = self;
    button.Clicked().Connect(
        [self, index]
        {
            if (auto* live = dynamic_cast<MenuBar*>(self.Get()))
            {
                if (live->GetCurrentIndex() == index)
                    live->CloseMenu();
                else
                    live->OpenMenu(index);
            }
        });
    return result;
}

Button& MenuBar::GetButton(int index) const
{
    VerifyAccess();
    if (index < 0 || static_cast<std::size_t>(index) >= m_Menus.size())
        throw std::out_of_range("Invalid menu-bar index.");
    auto* button = dynamic_cast<Button*>(m_Menus[static_cast<std::size_t>(index)].m_Button.Get());
    if (!button)
        throw std::logic_error("A menu-bar trigger was removed outside its composition API.");
    return *button;
}

ContextMenu& MenuBar::GetMenu(int index) const
{
    (void)GetButton(index);
    const auto& entry = m_Menus[static_cast<std::size_t>(index)];
    auto* menu = entry.m_Pending ? entry.m_Pending.get() : dynamic_cast<ContextMenu*>(entry.m_Menu.Get());
    if (!menu)
        throw std::logic_error("A menu-bar popup was removed outside its composition API.");
    return *menu;
}

int MenuBar::GetMenuCount() const
{
    VerifyAccess();
    return static_cast<int>(m_Menus.size());
}

int MenuBar::GetCurrentIndex() const
{
    VerifyAccess();
    for (std::size_t i = 0; i < m_Menus.size(); ++i)
        if (const auto* menu = dynamic_cast<ContextMenu*>(m_Menus[i].m_Menu.Get()); menu && menu->IsOpen())
            return static_cast<int>(i);
    return -1;
}

PopupSession MenuBar::OpenMenu(int index)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    auto& button = GetButton(index);
    if (!GetContext())
        throw std::logic_error("Attach the menu bar before opening a menu.");
    if (!IsVisible() || !IsEnabled() || !button.IsEnabled() || !button.IsVisible())
        return {};
    auto& entry = m_Menus[static_cast<std::size_t>(index)];
    if (entry.m_Pending)
        entry.m_Menu = WidgetRef(&GetContext()->AddOwnedPopup(std::move(entry.m_Pending), *this));
    auto& menu = GetMenu(index);
    if (menu.IsOpen())
        return GetContext()->GetPopupSession(menu);
    m_FocusedMenu = index;
    PopupOptions options;
    options.m_Placement = PopupPlacement::Below;
    options.m_AnchorWidget = &button;
    options.m_AllowOwnerInput = true;
    const WidgetRef trigger(&button);
    button.AddClass("menu-open");
    auto session = GetContext()->ShowPopup(menu, {}, options);
    session.OnFinished(
        [trigger](PopupResult)
        {
            if (auto* live = trigger.Get())
                live->RemoveClass("menu-open");
        });
    return session;
}

void MenuBar::CloseMenu()
{
    VerifyStructuralMutation();
    const auto current = GetCurrentIndex();
    if (current >= 0)
        GetMenu(current).Close();
}

void MenuBar::NavigateMenu(int direction, bool open)
{
    const int count = GetMenuCount();
    if (!count)
        return;
    int index = GetCurrentIndex();
    if (index < 0)
        index = m_FocusedMenu;
    for (int i = 0; i < count; ++i)
    {
        index = (index + direction + count) % count;
        const auto& button = GetButton(index);
        if (button.IsVisible() && button.IsEnabled())
        {
            m_FocusedMenu = index;
            if (open)
                OpenMenu(index);
            return;
        }
    }
}

bool MenuBar::IsFocusable() const
{
    VerifyAccess();
    return !m_Menus.empty();
}

SemanticInfo MenuBar::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Role = SemanticRole::MenuBar;
    return info;
}

bool MenuBar::OnInputPreview(const InputEvent& input)
{
    const auto* event = std::get_if<PointerEvent>(&input);
    if (!event)
        return Widget::OnInputPreview(input);
    if (!event->m_InsideClip)
        return false;
    if (event->m_Type != PointerEventType::Move &&
        !(event->m_Type == PointerEventType::Down && event->m_Button == PointerButton::Left))
        return false;
    for (int i = 0; i < GetMenuCount(); ++i)
    {
        auto& button = GetButton(i);
        if (!button.IsVisible() || !button.IsEnabled())
            continue;
        // Input coordinates are in the bar's layout space. Individual button transforms
        // use their own inverse so decorative movement keeps matching the trigger.
        Affine inverse;
        if (!button.GetStyle().m_Transform.Resolve(button.GetBounds()).Inverse(inverse) ||
            !ContainsRounded(button.GetBounds(), button.GetStyle().m_Radius, inverse.Map(event->m_Position)))
            continue;
        const int current = GetCurrentIndex();
        if (event->m_Type == PointerEventType::Down)
        {
            if (current == i)
                CloseMenu();
            else
                OpenMenu(i);
            return true;
        }
        if (current >= 0 && current != i)
        {
            OpenMenu(i);
            return true;
        }
        break;
    }
    return false;
}

bool MenuBar::OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers)
{
    if (!down || modifiers.m_Alt || modifiers.m_Control || modifiers.m_Meta)
        return false;
    if (key == Key::Left || key == Key::Right)
    {
        NavigateMenu(key == Key::Left ? -1 : 1, true);
        return true;
    }
    if (key == Key::Down || key == Key::Up || key == Key::Enter || key == Key::Space)
    {
        if (!repeat)
            NavigateMenu(0, true);
        return true;
    }
    return false;
}
} // namespace EStudioUI
