#pragma once

namespace EStudioUI
{
//! Keeps presentation deadlines stable across early input-event wakeups.
class FramePacer
{
  public:
    //! Creates a finite presentation rate limit in [1,1000] frames per second.
    explicit FramePacer(double framesPerSecond = 60);

    //! Returns whether a pending frame may be presented at this time.
    bool IsDue(double time) const;

    //! Accounts for rendering and swap time without scheduling catch-up bursts.
    void Presented(double started, double completed);

    //! Returns the next permitted presentation time.
    double GetDeadline() const;

  private:
    //! Minimum interval between presentation starts.
    double m_Interval;

    //! Absolute monotonic presentation deadline.
    double m_Deadline = 0;
};
} // namespace EStudioUI
