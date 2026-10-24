#pragma once
#include <EStudioUI/UiTimer.h>
#include <functional>

namespace EStudioUI::Detail
{
//! Bounded deadline queue shared only by owner-side timer operations.
class UiTimers
{
  public:
    //! Initializes the context's deadline queue.
    UiTimers();

    //! Clears callbacks before the context's state and widgets retire.
    ~UiTimers();

    //! Admits one finite nonnegative delay, with at most 1024 pending records.
    UiTimer Add(double now, double delay, std::function<void()> callback);

    //! Runs at most 64 entry-snapshot callbacks; callbacks scheduled by them await another turn.
    void Pump(double now);

    //! Returns the earliest pending GUI-clock deadline, or minus one.
    double NextDeadline() const;

  private:
    //! Retains queue metadata through callbacks that destroy their context.
    struct Data;

    //! Owns pending callback records on the UI owner.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI::Detail
