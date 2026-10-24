#include <EStudioUI/ValidationMessage.h>
#include <stdexcept>

namespace EStudioUI
{
ValidationMessage::ValidationMessage() : Label({}, "validationmessage")
{
    SetWordWrap(true);
    SetVisible(false);
}

void ValidationMessage::SetMessage(std::string message, FeedbackSeverity severity)
{
    VerifyStructuralMutation();
    if (severity < FeedbackSeverity::Information || severity > FeedbackSeverity::Error)
    {
        throw std::invalid_argument("Invalid feedback severity.");
    }
    m_Severity = severity;
    for (const auto* name : {"information", "success", "warning", "error"})
    {
        RemoveClass(name);
    }
    AddClass(severity == FeedbackSeverity::Error     ? "error"
             : severity == FeedbackSeverity::Warning ? "warning"
             : severity == FeedbackSeverity::Success ? "success"
                                                     : "information");
    SetVisible(!message.empty());
    SetText(std::move(message));
    NotifySemanticsChanged();
}

void ValidationMessage::Bind(TextBox& editor, std::string invalidMessage, std::string intermediateMessage)
{
    VerifyStructuralMutation();
    if (editor.GetContext() != GetContext())
    {
        throw std::invalid_argument("Validation feedback and editor must share a UI context.");
    }
    const WidgetRef self(this);
    auto update = [self, invalid = std::move(invalidMessage),
                   intermediate = std::move(intermediateMessage)](ValidationState state)
    {
        if (auto* live = dynamic_cast<ValidationMessage*>(self.Get()))
        {
            live->SetMessage(state == ValidationState::Invalid        ? invalid
                             : state == ValidationState::Intermediate ? intermediate
                                                                      : "",
                             state == ValidationState::Intermediate ? FeedbackSeverity::Warning
                                                                    : FeedbackSeverity::Error);
        }
    };
    m_Connection.emplace(editor.ValidationChanged().Connect(update));
    update(editor.GetValidationState());
}

FeedbackSeverity ValidationMessage::GetSeverity() const
{
    VerifyAccess();
    return m_Severity;
}

SemanticInfo ValidationMessage::DescribeSemantics() const
{
    auto info = Label::DescribeSemantics();
    info.m_Name = GetText();
    info.m_Live = m_Severity == FeedbackSeverity::Error ? SemanticLiveSetting::Assertive : SemanticLiveSetting::Polite;
    info.m_Description = m_Severity == FeedbackSeverity::Error     ? "Error"
                         : m_Severity == FeedbackSeverity::Warning ? "Warning"
                                                                   : "Information";
    return info;
}
} // namespace EStudioUI
