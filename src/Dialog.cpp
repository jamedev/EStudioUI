#include <EStudioUI/Dialog.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/UiContext.h>

namespace EStudioUI
{
Dialog::Dialog(std::string title) : Popup("dialog")
{
    SetFocusScope(true);
    auto& header = Add<Widget>("dialog-header");
    header.SetLayoutDirection(Axis::Horizontal);
    header.SetHeight(SizeRule::Content());
    header.SetAlignment(LayoutAlignment::Center);
    m_Title = &header.Add<Label>(std::move(title), "dialog-title");
    m_Title->SetWidth(SizeRule::Fill());
    auto& close = header.Add<Button>("x", "dialog-close");
    close.SetWidth(SizeRule::Fixed(30));
    close.SetHeight(SizeRule::Fixed(30));
    close.SetAccessibleName("Close dialog");
    m_Content = &Add<Widget>("dialog-content");
    m_Content->SetHeight(SizeRule::Content());
    m_Buttons = &Add<DialogButtonBox>();
    const WidgetRef self(this);
    close.Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<Dialog*>(self.Get()))
                live->Reject();
        });
    m_Buttons->Clicked().Connect(
        [self](StandardButton button)
        {
            if (auto* live = dynamic_cast<Dialog*>(self.Get()))
            {
                const auto role = DialogButtonBox::Role(button);
                if (role == DialogButtonRole::Accept || role == DialogButtonRole::Destructive)
                    live->Accept(static_cast<int>(button));
                else if (role == DialogButtonRole::Reject)
                    live->Reject(static_cast<int>(button));
            }
        });
    SetButtons({StandardButton::Ok, StandardButton::Cancel});
}

Dialog& Dialog::Create(Widget& owner, std::string title)
{
    owner.VerifyAccess();
    if (!owner.GetContext())
        throw std::logic_error("Attach the dialog owner before creating its dialog.");
    return static_cast<Dialog&>(owner.GetContext()->AddOwnedPopup(std::make_unique<Dialog>(std::move(title)), owner));
}

Widget& Dialog::Content() const
{
    VerifyAccess();
    return *m_Content;
}

DialogButtonBox& Dialog::Buttons() const
{
    VerifyAccess();
    return *m_Buttons;
}

void Dialog::SetTitle(std::string title)
{
    VerifyStructuralMutation();
    m_Title->SetText(std::move(title));
    NotifySemanticsChanged();
}

const std::string& Dialog::GetTitle() const
{
    VerifyAccess();
    return m_Title->GetText();
}

bool Dialog::Owns(const Widget& widget) const
{
    for (auto* current = &widget; current; current = current->GetParent())
        if (current == this)
            return true;
    return false;
}

void Dialog::SetButtons(std::vector<StandardButton> buttons)
{
    VerifyStructuralMutation();
    m_Buttons->SetButtons(std::move(buttons));
    Button* selected = nullptr;
    for (const auto value : m_Buttons->GetButtons())
        if (DialogButtonBox::Role(value) == DialogButtonRole::Accept)
        {
            selected = m_Buttons->GetButton(value);
            break;
        }
    SetDefaultButton(selected);
    m_Buttons->SetVisible(!m_Buttons->GetButtons().empty());
}

void Dialog::SetDefaultButton(Button* button)
{
    VerifyStructuralMutation();
    if (button && !Owns(*button))
        throw std::invalid_argument("A dialog default button must be its owned descendant.");
    if (auto* previous = m_Default.Get())
        previous->RemoveClass("dialog-default");
    m_Default = WidgetRef(button);
    if (button)
        button->AddClass("dialog-default");
}

void Dialog::SetInitialFocus(Widget* widget)
{
    VerifyStructuralMutation();
    if (widget && !Owns(*widget))
        throw std::invalid_argument("Initial dialog focus must be an owned descendant.");
    m_InitialFocus = WidgetRef(widget);
}

PopupSession Dialog::Open(bool modal, Widget* parentPopup)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    const bool alreadyOpen = IsOpen();
    PopupOptions options = modal ? PopupOptions::Modal() : PopupOptions{};
    options.m_DismissOnOutside = false;
    options.m_Modeless = !modal;
    options.m_Placement = PopupPlacement::Center;
    options.m_Parent = parentPopup;
    const WidgetRef self(this);
    auto session = Popup::Open(Point{}, options);
    if (!self.Get() || !session.IsPending() || alreadyOpen)
        return session;
    WidgetRef initial = m_InitialFocus;
    if (!initial.Get())
    {
        const auto find = [&](auto&& visit, Widget& node) -> Widget*
        {
            if (!node.IsVisible() || !node.IsEnabled())
                return nullptr;
            if (node.IsFocusable())
                return &node;
            for (const auto& child : node.GetChildren())
                if (auto* found = visit(visit, *child))
                    return found;
            return nullptr;
        };
        initial = WidgetRef(find(find, *m_Content));
        if (!initial.Get())
            initial = m_Default;
    }
    if (auto* target = initial.Get())
        GetContext()->SetFocus(target);
    return session;
}

void Dialog::Accept(int value)
{
    Close({PopupCloseReason::Accepted, value});
}

void Dialog::Reject(int value)
{
    Close({PopupCloseReason::Rejected, value});
}

SemanticInfo Dialog::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Role = SemanticRole::Dialog;
    info.m_Name = GetTitle();
    return info;
}

bool Dialog::OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers)
{
    if (!down || key != Key::Enter || modifiers.m_Control || modifiers.m_Alt || modifiers.m_Meta)
        return false;
    auto* focus = GetContext()->GetFocus();
    if (dynamic_cast<Button*>(focus))
        return false;
    if (const auto* editor = dynamic_cast<TextBox*>(focus);
        editor && editor->GetValidationState() != ValidationState::Acceptable)
        return true;
    if (auto* button = dynamic_cast<Button*>(m_Default.Get()); button && button->IsEnabled() && button->IsVisible())
    {
        if (!repeat)
            button->Trigger();
        return true;
    }
    return false;
}
} // namespace EStudioUI
