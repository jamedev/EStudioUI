#include <EStudioUI/FramePacer.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
FramePacer::FramePacer(double framesPerSecond)
{
    if (!std::isfinite(framesPerSecond) || framesPerSecond < 1 || framesPerSecond > 1000)
        throw std::invalid_argument("Frame rate must be in [1,1000].");
    m_Interval = 1 / framesPerSecond;
}

bool FramePacer::IsDue(double time) const
{
    return time >= m_Deadline;
}

void FramePacer::Presented(double started, double completed)
{
    if (!std::isfinite(started) || !std::isfinite(completed) || completed < started)
        throw std::invalid_argument("Presentation timestamps must be finite and monotonic.");
    m_Deadline = std::max(started + m_Interval, completed);
}

double FramePacer::GetDeadline() const
{
    return m_Deadline;
}
} // namespace EStudioUI
