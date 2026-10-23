#pragma once

#include "PopupSession.h"

#include <string>

namespace EStudioUI
{
//! An owned input outcome that remains readable after its editor or window is destroyed.
struct InputDialogResult
{
    //! Shared popup dismissal/response information.
    PopupResult m_Popup;
    //! Accepted canonical text; cancellations have no text result.
    std::optional<std::string> m_Text;
};

//! A nonblocking owner-side input result with no retained widget references.
class InputDialogSession
{
  public:
    //! Creates an empty result handle.
    InputDialogSession();

    //! Returns whether this presentation still awaits a result.
    bool IsPending() const;

    //! Returns the complete owned outcome, or nullopt while pending/empty.
    std::optional<InputDialogResult> GetResult() const;

    //! Runs once, immediately for an already completed handle; exceptions remain inspectable.
    void OnFinished(std::function<void(InputDialogResult)> callback) const;

    //! Returns the first completion-observer exception.
    std::exception_ptr GetCallbackError() const;

  private:
    friend class InputDialog;
    //! Combines the ordinary popup session with a per-presentation accepted value.
    InputDialogSession(PopupSession session, std::shared_ptr<std::optional<std::string>> text);
    //! Shares terminal state and observer errors with the common popup pipeline.
    PopupSession m_Session;
    //! Owns accepted bytes independently of the editor lifetime.
    std::shared_ptr<std::optional<std::string>> m_Text;
};
} // namespace EStudioUI
