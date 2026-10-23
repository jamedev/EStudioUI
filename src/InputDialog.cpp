#include <EStudioUI/InputDialog.h>
#include <EStudioUI/UiContext.h>

namespace EStudioUI
{
InputDialog::InputDialog(std::string title, std::string prompt, std::string text) : Dialog(std::move(title))
{
    AddClass("input-dialog");
    Content().SetGap(10);
    m_Prompt = &Content().Add<Label>("", "input-prompt");
    m_Prompt->SetWordWrap(true);
    m_Prompt->SetHeight(SizeRule::Content());
    m_Editor = &Content().Add<TextBox>(std::move(text));
    m_Editor->AddClass("dialog-editor");
    SetPrompt(std::move(prompt));
    SetInitialFocus(m_Editor);
}

InputDialog& InputDialog::Create(Widget& owner, std::string title, std::string prompt, std::string text)
{
    owner.VerifyAccess();
    if (!owner.GetContext())
        throw std::logic_error("Attach the input owner before creating its dialog.");
    return static_cast<InputDialog&>(owner.GetContext()->AddOwnedPopup(
        std::make_unique<InputDialog>(std::move(title), std::move(prompt), std::move(text)), owner));
}

TextBox& InputDialog::Editor() const
{
    VerifyAccess();
    return *m_Editor;
}

void InputDialog::SetPrompt(std::string prompt)
{
    VerifyStructuralMutation();
    m_Prompt->SetText(prompt);
    m_Editor->SetAccessibleName(std::move(prompt));
}

InputDialogSession InputDialog::Open(bool modal, Widget* parentPopup)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (IsOpen())
        return InputDialogSession(GetContext()->GetPopupSession(*this), m_ResultText);
    auto text = std::make_shared<std::optional<std::string>>();
    m_ResultText = text;
    auto popup = Dialog::Open(modal, parentPopup);
    return InputDialogSession(std::move(popup), std::move(text));
}

void InputDialog::Accept(int value)
{
    VerifyStructuralMutation();
    if (!IsOpen() || m_Editor->GetValidationState() != ValidationState::Acceptable)
        return;
    if (m_ResultText)
        *m_ResultText = m_Editor->GetText();
    Dialog::Accept(value);
}
} // namespace EStudioUI
