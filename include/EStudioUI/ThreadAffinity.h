#pragma once

#include <string_view>
#include <thread>

namespace EStudioUI
{
//! Captures one object's owner thread and diagnoses unsupported direct access.
class ThreadAffinity
{
  public:
    //! Captures the thread constructing this object.
    ThreadAffinity();

    //! Throws logic_error unless the current thread owns this object.
    void VerifyAccess(std::string_view operation = "UI object access") const;

    //! Returns whether the calling thread owns this object.
    bool IsOwnerThread() const noexcept;

    //! Returns the immutable owner thread identity.
    std::thread::id GetOwnerThreadId() const noexcept;

    //! Checks a copied owner identity without dereferencing its original object.
    static void VerifyThread(std::thread::id owner, std::string_view operation = "UI object access");

  private:
    //! Owner identity, unchanged throughout the object's lifetime.
    std::thread::id m_OwnerThread;
};
} // namespace EStudioUI
