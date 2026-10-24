#include <EStudioUI/WindowCloseRequest.h>

namespace EStudioUI
{
void WindowCloseRequest::Cancel()
{
    m_Cancelled = true;
}

bool WindowCloseRequest::IsCancelled() const
{
    return m_Cancelled;
}
} // namespace EStudioUI
