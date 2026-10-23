#include <EStudioUI/InputDialogSession.h>

#include <stdexcept>

namespace EStudioUI
{
InputDialogSession::InputDialogSession() = default;

InputDialogSession::InputDialogSession(PopupSession session, std::shared_ptr<std::optional<std::string>> text)
    : m_Session(std::move(session)), m_Text(std::move(text))
{
}

bool InputDialogSession::IsPending() const
{
    return m_Session.IsPending();
}

std::optional<InputDialogResult> InputDialogSession::GetResult() const
{
    const auto popup = m_Session.GetResult();
    if (!popup)
        return {};
    return InputDialogResult{*popup, popup->m_Reason == PopupCloseReason::Accepted && m_Text ? *m_Text : std::nullopt};
}

void InputDialogSession::OnFinished(std::function<void(InputDialogResult)> callback) const
{
    if (!callback)
        throw std::invalid_argument("An input completion requires a callback.");
    m_Session.OnFinished(
        [text = m_Text, callback = std::move(callback)](PopupResult result)
        { callback({result, result.m_Reason == PopupCloseReason::Accepted && text ? *text : std::nullopt}); });
}

std::exception_ptr InputDialogSession::GetCallbackError() const
{
    return m_Session.GetCallbackError();
}
} // namespace EStudioUI
