#pragma once

#include "Animation.h"

#include <cstdint>

namespace EStudioUI
{
class UiContext;

//! A window-local animation identity; zero is never scheduled.
using AnimationId = std::uint64_t;

//! Observable playback state; completed or unknown identities are Stopped.
enum class AnimationState
{
    Stopped,
    Running,
    Paused
};

//! Cancellation policy for the remaining tracks.
enum class AnimationStopMode
{
    KeepCurrent,
    ReleaseStyle,
    Finish
};

//! Schedules synchronized property tracks on the GUI thread without a timer thread.
class AnimationSystem
{
  public:
    //! Observes the context that owns this scheduler.
    explicit AnimationSystem(UiContext& context);

    //! Releases tracks without invoking callbacks.
    ~AnimationSystem();

    //! A scheduler belongs to one context.
    AnimationSystem(const AnimationSystem&) = delete;

    //! A scheduler belongs to one context.
    AnimationSystem& operator=(const AnimationSystem&) = delete;

    //! Starts a definition and replaces older writers of the same target/channel.
    AnimationId Play(Animation animation);

    //! Freezes playback at its current position.
    bool Pause(AnimationId id);

    //! Resumes a paused animation without counting paused wall time.
    bool Resume(AnimationId id);

    //! Samples elapsed seconds including delay, retaining paused/running state until completion.
    bool Seek(AnimationId id, double seconds);

    //! Stops one animation; Finish applies its nominal final iteration endpoint.
    bool Stop(AnimationId id, AnimationStopMode mode = AnimationStopMode::KeepCurrent);

    //! Stops current animations; callbacks may safely schedule new definitions.
    void StopAll(AnimationStopMode mode = AnimationStopMode::KeepCurrent);

    //! Returns the state of a live timeline.
    AnimationState GetState(AnimationId id) const;

    //! Returns whether any live target has a running timeline, including initial delay.
    bool HasRunning() const;

    //! Advances a finite nondecreasing absolute clock; UiContext calls this before layout.
    void Update(double time);

    //! Returns the current absolute GUI clock, including during animation callbacks.
    double GetTime() const;

    //! Emitted once after natural completion or an explicit Finish operation.
    Signal<AnimationId>& Finished();

    //! Emitted once when cancellation, replacement, or target deletion ends a timeline.
    Signal<AnimationId>& Cancelled();

  private:
    //! Hides scheduling and callback-lifetime bookkeeping.
    struct Impl;

    //! Owns scheduler state.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
