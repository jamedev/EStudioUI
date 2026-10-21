#pragma once

#include "EasingCurve.h"
#include "Widget.h"

#include <functional>
#include <initializer_list>
#include <stdexcept>

namespace EStudioUI
{
//! Interpolates a finite scalar without restricting overshooting easing curves.
float Tween(float from, float to, double progress);

//! Interpolates a finite double-precision scalar.
double Tween(double from, double to, double progress);

//! Interpolates a position or displacement.
Point Tween(Point from, Point to, double progress);

//! Interpolates a rectangle while keeping dimensions nonnegative.
Rect Tween(Rect from, Rect to, double progress);

//! Interpolates premultiplied CSS color components, then returns straight RGBA.
Color Tween(Color from, Color to, double progress);

//! Interpolates edge distances while keeping them nonnegative.
Insets Tween(Insets from, Insets to, double progress);

//! Interpolates shadow offsets, blur, and color.
Shadow Tween(Shadow from, Shadow to, double progress);

//! Timing shared by every track in an animation.
struct AnimationOptions
{
    //! Duration of one iteration in seconds; zero applies the endpoint immediately.
    double m_Duration = 0.3;

    //! Initial delay in seconds; starting values are held during the delay.
    double m_Delay = 0;

    //! Time-to-progress curve, shared by all tracks.
    EasingCurve m_Easing = EasingCurve();

    //! Total iterations; zero repeats until explicitly stopped.
    unsigned m_Iterations = 1;

    //! Runs every second iteration backwards.
    bool m_Alternate = false;

    //! Rejects invalid durations and infinite zero-duration repetition.
    void Validate() const;
};

//! A move-only collection of synchronized, lifetime-safe property tracks.
class Animation
{
  public:
    //! Creates a definition using validated timing options.
    explicit Animation(AnimationOptions options = {});

    //! Transfers an animation definition into its owning system.
    Animation(Animation&&) noexcept = default;

    //! Transfers a definition before playback.
    Animation& operator=(Animation&&) noexcept = default;

    //! Each definition has one owner.
    Animation(const Animation&) = delete;

    //! Each definition has one owner.
    Animation& operator=(const Animation&) = delete;

    //! Adds a scalar, point, rectangle, color, insets, or shadow setter track.
    template <typename T, typename Setter>
    Animation& AddValue(Widget& widget, std::string channel, T from, T to, Setter setter)
    {
        if (channel.empty())
            throw std::invalid_argument("An animation property channel cannot be empty.");
        // Validate both endpoints before the definition can be scheduled.
        static_cast<void>(Tween(from, to, 0));
        return AddCustom(widget, std::move(channel),
                         [from, to, setter = std::move(setter)](Widget& target, double progress) mutable
                         { setter(target, Tween(from, to, progress)); });
    }

    //! Adds a custom interpolator; callbacks receive the live target and eased progress.
    Animation& AddCustom(Widget& widget, std::string channel, std::function<void(Widget&, double)> update);

    //! Adds one typed style track; optional release restores CSS on natural completion.
    Animation& AddStyleProperty(Widget& widget, StyleProperty property, StyleValue from, StyleValue to,
                                bool releaseOnFinish = false);

    //! Adds changed or explicitly selected style properties, including discrete endpoint changes.
    Animation& AddStyle(Widget& widget, const Style& from, const Style& to,
                        std::initializer_list<StyleProperty> properties = {});

    //! Adds absolute window-coordinate geometry; the widget leaves normal layout flow.
    Animation& AddBounds(Widget& widget, Rect from, Rect to);

    //! Adds layout-preserving translation of a widget and its flow descendants.
    Animation& AddTranslation(Widget& widget, Point from, Point to);

  private:
    friend class AnimationSystem;
    friend class UiContext;

    //! One independent property writer observed without owning its target.
    struct Track
    {
        //! Observes the target's lifetime.
        WidgetRef m_Target;

        //! Identifies a property for replacement arbitration.
        std::string m_Channel;

        //! Applies one eased sample on the GUI thread.
        std::function<void(Widget&, double)> m_Update;

        //! Releases a temporary CSS override after successful completion.
        std::function<void(Widget&)> m_Release;

        //! Stops superseded or destroyed-target tracks without invalidating iteration.
        bool m_Active = true;

        //! Discrete styles change at the iteration endpoint, independently of easing overshoot.
        bool m_Discrete = false;

        //! Restores CSS after natural completion instead of retaining a final override.
        bool m_ReleaseOnFinish = false;
    };

    //! Stores validated timing.
    AnimationOptions m_Options;

    //! Owns independent tracks before playback.
    std::vector<Track> m_Tracks;

    //! Commits a stylesheet's nominal endpoint before temporary overrides are released.
    std::function<void()> m_OnFinish;
};
} // namespace EStudioUI
