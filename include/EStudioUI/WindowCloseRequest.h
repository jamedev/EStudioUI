#pragma once

namespace EStudioUI
{
//! Synchronous veto for a window-close intent; asynchronous work retries Close after approval.
class WindowCloseRequest
{
  public:
    //! Prevents this close intent from hiding or expiring the window.
    void Cancel();
    //! Returns whether any close observer vetoed this intent.
    bool IsCancelled() const;

  private:
    //! Cancellation is monotonic during one notification.
    bool m_Cancelled = false;
};
} // namespace EStudioUI
