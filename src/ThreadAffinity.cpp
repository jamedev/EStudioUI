#include <EStudioUI/ThreadAffinity.h>

#include <stdexcept>
#include <string>

namespace EStudioUI
{
ThreadAffinity::ThreadAffinity() : m_OwnerThread(std::this_thread::get_id())
{
}

void ThreadAffinity::VerifyAccess(std::string_view operation) const
{
    VerifyThread(m_OwnerThread, operation);
}

bool ThreadAffinity::IsOwnerThread() const noexcept
{
    return std::this_thread::get_id() == m_OwnerThread;
}

std::thread::id ThreadAffinity::GetOwnerThreadId() const noexcept
{
    return m_OwnerThread;
}

void ThreadAffinity::VerifyThread(std::thread::id owner, std::string_view operation)
{
    if (std::this_thread::get_id() != owner)
        throw std::logic_error(std::string(operation) +
                               " requires the owning UI thread; use a UI handle or a task continuation.");
}
} // namespace EStudioUI
