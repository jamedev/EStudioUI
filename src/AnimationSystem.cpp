#include <EStudioUI/AnimationSystem.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace EStudioUI
{
struct AnimationSystem::Impl
{
    struct Entry
    {
        AnimationId m_Id;
        Animation m_Animation;
        AnimationState m_State = AnimationState::Running;
        double m_Anchor = 0;
        double m_Elapsed = 0;
        bool m_Sampling = false;
    };

    // Public callbacks may mutate the scheduler. Entries remain stable and vector
    // removal waits until the outermost dispatch has returned.
    struct Dispatch
    {
        Impl& m_Owner;

        explicit Dispatch(Impl& owner) : m_Owner(owner)
        {
            ++m_Owner.m_Depth;
        }

        ~Dispatch()
        {
            if (--m_Owner.m_Depth == 0)
                std::erase_if(m_Owner.m_Entries,
                              [](const auto& entry) { return entry->m_State == AnimationState::Stopped; });
        }
    };

    explicit Impl(UiContext& context) : m_Context(context)
    {
    }

    std::shared_ptr<Entry> Find(AnimationId id) const
    {
        const auto found = std::find_if(m_Entries.begin(), m_Entries.end(), [&](const auto& entry)
                                        { return entry->m_Id == id && entry->m_State != AnimationState::Stopped; });
        return found == m_Entries.end() ? nullptr : *found;
    }

    bool Live(const Animation::Track& track) const
    {
        const auto* widget = track.m_Target.Get();
        return track.m_Active && widget && widget->GetContext() == &m_Context;
    }

    bool HasTargets(const Entry& entry) const
    {
        return std::any_of(entry.m_Animation.m_Tracks.begin(), entry.m_Animation.m_Tracks.end(),
                           [&](const auto& track) { return Live(track); });
    }

    void Cancel(const std::shared_ptr<Entry>& entry, bool release)
    {
        if (entry->m_State == AnimationState::Stopped)
            return;
        entry->m_State = AnimationState::Stopped;
        for (auto& track : entry->m_Animation.m_Tracks)
        {
            if (release && Live(track) && track.m_Release)
                track.m_Release(*track.m_Target.Get());
            track.m_Active = false;
        }
        m_Context.RequestRepaint();
        m_Cancelled.Emit(entry->m_Id);
    }

    void Sample(const std::shared_ptr<Entry>& entry, double elapsed, bool forceFinish = false)
    {
        if (entry->m_State == AnimationState::Stopped)
            return;
        if (entry->m_Sampling)
            throw std::logic_error("An animation setter cannot recursively seek or finish its own timeline.");

        struct SamplingScope
        {
            bool& m_Sampling;

            explicit SamplingScope(bool& sampling) : m_Sampling(sampling)
            {
                m_Sampling = true;
            }

            ~SamplingScope()
            {
                m_Sampling = false;
            }
        } samplingScope(entry->m_Sampling);

        if (!HasTargets(*entry))
        {
            Cancel(entry, false);
            return;
        }
        const auto& options = entry->m_Animation.m_Options;
        const double active = std::max(0.0, elapsed - options.m_Delay);
        const bool complete =
            forceFinish || (elapsed >= options.m_Delay &&
                            (options.m_Duration == 0 ||
                             (options.m_Iterations && active >= options.m_Duration * options.m_Iterations)));
        double normalized = 0;
        bool reversed = false;
        if (complete)
        {
            normalized = 1;
            reversed = options.m_Alternate && options.m_Iterations && options.m_Iterations % 2 == 0;
        }
        else if (elapsed >= options.m_Delay && options.m_Duration > 0)
        {
            const double iteration = std::floor(active / options.m_Duration);
            normalized = std::fmod(active, options.m_Duration) / options.m_Duration;
            reversed = options.m_Alternate && std::fmod(iteration, 2.0) >= 1;
        }
        const double progress = options.m_Easing.Evaluate(reversed ? 1 - normalized : normalized);
        const double discrete = reversed ? (normalized >= 1 ? 0 : 1) : (normalized >= 1 ? 1 : 0);
        try
        {
            for (auto& track : entry->m_Animation.m_Tracks)
            {
                if (entry->m_State == AnimationState::Stopped)
                    return;
                if (!Live(track))
                    continue;
                track.m_Update(*track.m_Target.Get(), track.m_Discrete ? discrete : progress);
            }
        }
        catch (...)
        {
            // A throwing writer is not called again on the next frame.
            entry->m_State = AnimationState::Stopped;
            for (auto& track : entry->m_Animation.m_Tracks)
                track.m_Active = false;
            throw;
        }
        if (entry->m_State == AnimationState::Stopped)
            return;
        m_Context.RequestRepaint();
        if (!HasTargets(*entry))
        {
            Cancel(entry, false);
            return;
        }
        if (!complete)
            return;
        entry->m_State = AnimationState::Stopped;
        if (entry->m_Animation.m_OnFinish)
            entry->m_Animation.m_OnFinish();
        for (auto& track : entry->m_Animation.m_Tracks)
        {
            if (Live(track) && track.m_ReleaseOnFinish && track.m_Release)
                track.m_Release(*track.m_Target.Get());
            track.m_Active = false;
        }
        m_Finished.Emit(entry->m_Id);
    }

    UiContext& m_Context;
    std::vector<std::shared_ptr<Entry>> m_Entries;
    AnimationId m_NextId = 1;
    double m_Time = 0;
    unsigned m_Depth = 0;
    bool m_Updating = false;
    Signal<AnimationId> m_Finished;
    Signal<AnimationId> m_Cancelled;
};

AnimationSystem::AnimationSystem(UiContext& context) : m_Impl(std::make_unique<Impl>(context))
{
}

AnimationSystem::~AnimationSystem() = default;

AnimationId AnimationSystem::Play(Animation animation)
{
    auto& self = *m_Impl;
    animation.m_Options.Validate();
    if (animation.m_Tracks.empty())
        throw std::invalid_argument("An animation requires at least one track.");
    for (const auto& track : animation.m_Tracks)
        if (!self.Live(track))
            throw std::invalid_argument("Animation targets must belong to this UI context.");
    if (self.m_NextId == std::numeric_limits<AnimationId>::max())
        throw std::overflow_error("Animation identity space exhausted.");
    Impl::Dispatch dispatch(self);
    auto entry = std::make_shared<Impl::Entry>(
        Impl::Entry{self.m_NextId++, std::move(animation), AnimationState::Running, self.m_Time, 0});
    // Retire overlapping writers before sampling the new definition. Cancellation
    // listeners run afterwards, so a listener's newer Play() wins consistently.
    std::vector<std::shared_ptr<Impl::Entry>> cancelled;
    for (const auto& previous : self.m_Entries)
    {
        if (previous->m_State == AnimationState::Stopped)
            continue;
        for (auto& oldTrack : previous->m_Animation.m_Tracks)
            for (const auto& newTrack : entry->m_Animation.m_Tracks)
                if (oldTrack.m_Target.Get() == newTrack.m_Target.Get() && oldTrack.m_Channel == newTrack.m_Channel)
                    oldTrack.m_Active = false;
        if (!self.HasTargets(*previous))
        {
            previous->m_State = AnimationState::Stopped;
            cancelled.push_back(previous);
        }
    }
    self.m_Entries.push_back(entry);
    self.Sample(entry, 0);
    for (const auto& previous : cancelled)
        self.m_Cancelled.Emit(previous->m_Id);
    return entry->m_Id;
}

bool AnimationSystem::Pause(AnimationId id)
{
    auto& self = *m_Impl;
    const auto entry = self.Find(id);
    if (!entry || entry->m_State != AnimationState::Running)
        return false;
    entry->m_Elapsed += self.m_Time - entry->m_Anchor;
    entry->m_State = AnimationState::Paused;
    self.m_Context.RequestRepaint();
    return true;
}

bool AnimationSystem::Resume(AnimationId id)
{
    auto& self = *m_Impl;
    const auto entry = self.Find(id);
    if (!entry || entry->m_State != AnimationState::Paused)
        return false;
    entry->m_Anchor = self.m_Time;
    entry->m_State = AnimationState::Running;
    self.m_Context.RequestRepaint();
    return true;
}

bool AnimationSystem::Seek(AnimationId id, double seconds)
{
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::invalid_argument("Animation seek time must be finite and nonnegative.");
    auto& self = *m_Impl;
    const auto entry = self.Find(id);
    if (!entry)
        return false;
    Impl::Dispatch dispatch(self);
    entry->m_Elapsed = seconds;
    entry->m_Anchor = self.m_Time;
    self.Sample(entry, seconds);
    return true;
}

bool AnimationSystem::Stop(AnimationId id, AnimationStopMode mode)
{
    auto& self = *m_Impl;
    const auto entry = self.Find(id);
    if (!entry)
        return false;
    Impl::Dispatch dispatch(self);
    if (mode == AnimationStopMode::Finish)
        self.Sample(entry, 0, true);
    else
        self.Cancel(entry, mode == AnimationStopMode::ReleaseStyle);
    return true;
}

void AnimationSystem::StopAll(AnimationStopMode mode)
{
    auto& self = *m_Impl;
    Impl::Dispatch dispatch(self);
    const auto count = self.m_Entries.size();
    for (std::size_t i = 0; i < count; ++i)
        Stop(self.m_Entries[i]->m_Id, mode);
}

AnimationState AnimationSystem::GetState(AnimationId id) const
{
    const auto entry = m_Impl->Find(id);
    return entry ? entry->m_State : AnimationState::Stopped;
}

bool AnimationSystem::HasRunning() const
{
    const auto& self = *m_Impl;
    return std::any_of(self.m_Entries.begin(), self.m_Entries.end(), [&](const auto& entry)
                       { return entry->m_State == AnimationState::Running && self.HasTargets(*entry); });
}

void AnimationSystem::Update(double time)
{
    auto& self = *m_Impl;
    if (!std::isfinite(time) || time < self.m_Time)
        throw std::invalid_argument("Animation time must be finite and nondecreasing.");
    if (self.m_Updating)
        throw std::logic_error("Animation clock updates cannot be nested inside animation callbacks.");
    self.m_Time = time;
    if (self.m_Entries.empty())
        return;
    Impl::Dispatch dispatch(self);

    struct UpdateScope
    {
        bool& m_Updating;

        explicit UpdateScope(bool& updating) : m_Updating(updating)
        {
            m_Updating = true;
        }

        ~UpdateScope()
        {
            m_Updating = false;
        }
    } updateScope(self.m_Updating);

    const auto count = self.m_Entries.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto entry = self.m_Entries[i];
        if (entry->m_State == AnimationState::Running)
            self.Sample(entry, entry->m_Elapsed + time - entry->m_Anchor);
        else if (entry->m_State == AnimationState::Paused && !self.HasTargets(*entry))
            self.Cancel(entry, false);
    }
}

Signal<AnimationId>& AnimationSystem::Finished()
{
    return m_Impl->m_Finished;
}

double AnimationSystem::GetTime() const
{
    return m_Impl->m_Time;
}

Signal<AnimationId>& AnimationSystem::Cancelled()
{
    return m_Impl->m_Cancelled;
}
} // namespace EStudioUI
