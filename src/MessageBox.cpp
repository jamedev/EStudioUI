#include <EStudioUI/MessageBox.h>
#include <EStudioUI/UiContext.h>

namespace EStudioUI
{
MessageBox::MessageBox(std::string title, std::string message) : Dialog(std::move(title))
{
    AddClass("message-box");
    AddClass("message-information");
    m_Message = &Content().Add<Label>("", "message-text");
    m_Message->SetWordWrap(true);
    m_Message->SetHeight(SizeRule::Content());
    SetMessage(std::move(message));
    SetButtons({StandardButton::Ok});
}

MessageBox& MessageBox::Create(Widget& owner, std::string title, std::string message)
{
    owner.VerifyAccess();
    if (!owner.GetContext())
        throw std::logic_error("Attach the message owner before creating its dialog.");
    return static_cast<MessageBox&>(
        owner.GetContext()->AddOwnedPopup(std::make_unique<MessageBox>(std::move(title), std::move(message)), owner));
}

void MessageBox::SetMessage(std::string message)
{
    VerifyStructuralMutation();
    m_Message->SetText(message);
    SetAccessibleDescription(std::move(message));
}

const std::string& MessageBox::GetMessage() const
{
    VerifyAccess();
    return m_Message->GetText();
}

void MessageBox::SetKind(MessageKind kind)
{
    VerifyStructuralMutation();
    if (kind < MessageKind::Information || kind > MessageKind::Error)
        throw std::invalid_argument("Unknown message kind.");
    if (kind == m_Kind)
        return;
    static constexpr const char* classes[]{"message-information", "message-question", "message-warning",
                                           "message-error"};
    RemoveClass(classes[static_cast<std::size_t>(m_Kind)]);
    m_Kind = kind;
    AddClass(classes[static_cast<std::size_t>(m_Kind)]);
}

MessageKind MessageBox::GetKind() const
{
    VerifyAccess();
    return m_Kind;
}
} // namespace EStudioUI
