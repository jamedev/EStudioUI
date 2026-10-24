#pragma once
#include "Label.h"
#include "TextBox.h"

namespace EStudioUI
{
//! Feedback severity shared by validation, notifications and logs.
enum class FeedbackSeverity
{
    Information,
    Success,
    Warning,
    Error
};

//! Inline accessible feedback with optional lifetime-bound validator observation.
class ValidationMessage : public Label
{
  public:
    //! Creates an initially empty message.
    ValidationMessage();
    //! Changes owned text and severity; empty text hides the message.
    void SetMessage(std::string message, FeedbackSeverity severity = FeedbackSeverity::Error);
    //! Observes an editor's validation; the editor may be destroyed first.
    void Bind(TextBox& editor, std::string invalidMessage, std::string intermediateMessage = {});
    //! Returns the current severity.
    FeedbackSeverity GetSeverity() const;

  protected:
    //! Exposes the message text and its severity to accessibility clients.
    SemanticInfo DescribeSemantics() const override;

  private:
    //! Current severity.
    FeedbackSeverity m_Severity = FeedbackSeverity::Error;
    //! Receiver-scoped validator connection.
    std::optional<ScopedConnection> m_Connection;
};
} // namespace EStudioUI
