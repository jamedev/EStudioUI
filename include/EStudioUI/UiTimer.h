#pragma once
#include "ThreadAffinity.h"
#include <exception>
#include <memory>

namespace EStudioUI
{
namespace Detail
{
struct UiTimerRecord;
class UiTimers;
} // namespace Detail

//! Owns a cancellable owner-thread deadline without requesting animation frames.
class UiTimer
{
  public:
    //! Creates an inactive timer.
    UiTimer();

    //! Cancels a pending callback on its owner.
    ~UiTimer();

    //! Keeps one owner of cancellation responsibility.
    UiTimer(const UiTimer&) = delete;

    //! Keeps one owner of cancellation responsibility.
    UiTimer& operator=(const UiTimer&) = delete;

    //! Transfers the deadline handle.
    UiTimer(UiTimer&& other) noexcept;

    //! Cancels the previous deadline before transferring ownership.
    UiTimer& operator=(UiTimer&& other) noexcept;

    //! Cancels once; false means already completed or inactive.
    bool Cancel();

    //! Returns whether the callback still awaits its deadline.
    bool IsPending() const;

    //! Returns a contained callback exception after completion.
    std::exception_ptr GetError() const;

  private:
    friend class Detail::UiTimers;
    //! Adopts a scheduled owner-side record.
    explicit UiTimer(std::shared_ptr<Detail::UiTimerRecord> record);

    //! Diagnoses unsupported cross-thread use before touching callback storage.
    ThreadAffinity m_Affinity;

    //! Retains its terminal outcome independently of context lifetime.
    std::shared_ptr<Detail::UiTimerRecord> m_Record;
};
} // namespace EStudioUI
