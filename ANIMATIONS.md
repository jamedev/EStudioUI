# Animation system / EStudioUI 0.4

The animation system drives current computed CSS properties and arbitrary
application properties on one GUI-thread timeline. It adds no runtime dependency.
Interpolation runs on the CPU; existing widgets continue through the GPU renderer.
The GLFW loop requests frames while a timeline is running and returns to event
waiting when animations finish or pause. Playback uses elapsed time, so skipped
frames do not slow down the animation.

## Transition between stylesheets

```cpp
#include <EStudioUI/UiContext.h>
#include <stdexcept>

// The UI already contains a widget whose ID is "card".
EStudioUI::AnimationOptions options;
options.m_Duration = 0.8; // Seconds, independently of display refresh rate.
options.m_Easing = EStudioUI::EasingCurve(EStudioUI::EasingCurve::Kind::EaseInOut);

std::string error;
const auto id = ui.AnimateStyleSheet(
    R"css(
        window { padding: 20px; }
        #card { width: 180px; height: 100px; background: #244080;
                border-radius: 8px; translate-x: 0px; }
    )css",
    R"css(
        window { padding: 20px; }
        #card { width: 340px; height: 160px; background: #168868;
                border-radius: 24px; translate-x: 120px; }
    )css",
    options, error);
if (id == 0)
    throw std::runtime_error(error);
```

The inputs are **complete stylesheets**, using the existing CSS subset. An
application can prepend its common theme to each endpoint. Both inputs parse
before anything changes; an invalid input retains the current stylesheet and
running transition. Invalid timing options throw before mutation.

`ui.AnimateStyleSheet(finalCss, options, error)` starts from current presentation,
including an interrupted transition. This overload avoids a jump when retargeting.
The explicit two-sheet overload intentionally starts from its supplied initial
sheet. Only one whole-stylesheet transition is owned by each context.

Selectors, inheritance, and endpoint geometry are resolved once for existing
widgets. Only changed computed properties become tracks; CSS is not parsed every
frame. The final sheet is installed underneath temporary presentation overrides.
Natural completion releases those overrides, so subsequent states and theme
reloads use normal CSS again. New widgets use the current underlying sheet and
normal inherited presentation; they are not retroactively added to the snapshot.
Interaction-state changes still affect properties not currently owned by a track.

## Synchronized typed properties

```cpp
using namespace EStudioUI;

Animation group(options);
group.AddTranslation(button, {0, 0}, {120, 25});
group.AddStyleProperty(editor, StyleProperty::Background,
                       editor.GetStyle().m_Background, Color{0.1f, 0.4f, 0.3f, 1});
group.AddBounds(floatingPanel, {40, 80, 200, 100}, {360, 120, 320, 180});
group.AddValue(widthPanel, "width-contract", 120.0f, 240.0f,
    [](Widget& target, float width)
    {
        target.SetWidth(SizeRule::Fixed(width));
    });
const auto groupId = ui.Animations().Play(std::move(group));
```

`AddValue` supports `float`, `double`, `Point`, `Rect`, `Color`, `Insets`, and
`Shadow`. Angles, zoom, camera parameters, scroll offsets, progress values, and
other application magnitudes can use scalar tracks with their own setters.
`AddCustom` accepts a live widget and eased progress for arbitrary types or
application-defined interpolation. An embedded scene's setter should call its
`OpenGLWidget::RequestFrame()` when scene data changes.

`AddStyle` accepts two computed `Style` objects and optionally a list of
`StyleProperty` identifiers. Its default adds changed properties; an explicit
list adds those selected properties. `SetStyleProperty` also exposes the same
typed overrides without a timeline. `ClearStyleProperty` / `ClearStyleProperties`
return those values to the cascade.

Direct property animations retain their final values. `AddStyleProperty` accepts
`releaseOnFinish = true` when a temporary override should instead return to CSS.
Keep callback captures lifetime-safe: the scheduler supplies a live target, but
does not make unrelated captured raw pointers safe.

## Interpolation and geometry

| Property | Behavior |
| --- | --- |
| All current computed colors | Premultiplied RGBA interpolation in CSS component space, not linear-light RGB; avoids dark fringes when fading to transparency |
| Opacity, corners, borders, font size, spacing, collection metrics | Numeric interpolation with valid-range clamping |
| Padding / margin | All four edges interpolate independently |
| Shadow | Both offsets, blur, and color interpolate |
| Width / height with matching units | Interpolate pixels, percentages, or positive Fill weights |
| Stylesheet dimensions with different units | Capture resolved endpoint pixel sizes, including intrinsic/content and Fill geometry |
| `translate-x`, `translate-y` | Signed logical-pixel translation after layout; normal-flow descendants, clipping, and hit testing follow |
| Display, flow direction, alignments | Discrete changes at each iteration endpoint, independent of easing overshoot |
| Absolute bounds | Interpolate window-coordinate position and dimensions outside ordinary flow |

Translation leaves the widget's allocated flow slot unchanged. Absolute placement
uses `SetFloatingBounds`; `ClearFloatingBounds` restores normal flow. Existing
absolute children retain their window-coordinate placement. Built-in owners such
as tables calculate their floating editor bounds from their current content area.
The renderer still draws axis-aligned widget rectangles; arbitrary rotation and
new CSS properties require corresponding widget/renderer support, even though
their application parameters can already be animated.

Code-level Fixed/Content/Fill, direction, and gap contracts retain precedence over
CSS presentation overrides. Animate such a contract through its actual setter,
as in the width example. Mixed-unit direct `AddStyleProperty` tracks can resolve
pixels against percentages; mixed intrinsic/Fill units require measured pixel
endpoints and are rejected with a diagnostic. Whole-stylesheet transitions
perform that measurement automatically.

Mixed-unit pixel endpoints are captured at transition creation. If the window,
intrinsic content, or layout structure changes during playback, normal final CSS
layout may differ when the temporary dimension overrides are released. Retarget
the transition when a new responsive endpoint is needed. This is interpolation
between measured endpoint states, not a constraint solver or a layout-morph
algorithm for changes to row/column structure.

Paint-only backgrounds, opacity, border colors, corners, shadows, and collection
colors reuse existing style/layout caches. Inherited colors, typography,
translation, and layout dimensions invalidate the relevant current full-tree
style/layout pass. Text reshapes when font size changes. Animating thousands of
layout-affecting properties is not claimed to be constant-cost.

Hover also refreshes when geometry moves under a stationary pointer. Endpoint
measurement does not prematurely remove focus from a widget that will only be
hidden at the end of its animation.

## Curves and playback

There are 22 presets: Linear; CSS Ease, EaseIn, EaseOut, EaseInOut; Quad, Cubic,
Sine, Expo, and Back in/out/in-out; OutBounce; and OutElastic. Back and Elastic
preserve overshoot for positions and custom scalars. Colors and constrained
dimensions retain valid ranges. `EasingCurve::CubicBezier(x1, y1, x2, y2)` supports
custom CSS-style curves with x in [0,1] and finite y in [-1000,1000]. It inverts
the x coordinate before evaluating y.

`AnimationOptions` also provides an initial delay, iteration count, and alternate
direction. Zero iterations repeat indefinitely; a zero duration applies the
nominal final endpoint immediately after the delay and cannot repeat forever.
Finite alternating playback with an even iteration count ends at the start.
Explicit two-sheet transitions restore the initial sheet in that case. When
retargeting an interrupted presentation, the one-sheet overload's return leg
restores the previously underlying sheet, which may differ from the interrupted
presentation; use explicit endpoints when that distinction matters.

```cpp
auto& animations = ui.Animations();
animations.Pause(id);
animations.Seek(id, 0.4); // Elapsed seconds including delay; works while paused.
animations.Resume(id);
animations.Stop(id, AnimationStopMode::Finish);       // Exact nominal endpoint.
animations.Stop(id, AnimationStopMode::ReleaseStyle); // Return owned styles to CSS.
animations.Stop(id);                                 // Freeze current values.
```

Seeking to the end of a finite timeline completes it, including while paused.
Completed identities are retired. Frozen styles are persistent overrides until
explicitly cleared; use ReleaseStyle when cancelling a CSS transition that
should return to its underlying sheet. A normal SetStyleSheet/LoadStyleSheet
releases an active whole-sheet transition. Finished and Cancelled signals report
timeline identities; completion listeners may schedule a following animation.

New tracks replace older writers of the same widget/property channel. Unrelated
tracks continue. A custom callback's channel must identify the property it owns;
the scheduler cannot infer which properties an arbitrary callback modifies.
Destroyed or detached targets are skipped safely. Hidden targets keep advancing
so their state is current when shown. All operations belong on the GUI thread.
Writer exceptions retire the failing timeline and propagate. Recursive clock
updates or recursively seeking/finishing the currently sampling timeline are
rejected, rather than recursing through setter callbacks.

Window drives the clock automatically. Custom hosts call `UiContext::SetTime`
with finite, nondecreasing time before Prepare/Paint; `GetTime` reads that same
clock. Tests use paused timelines plus Seek to produce deterministic samples.

## Playground and verification

The **Animations** tab offers CSS endpoints, a typed property group, duration,
four selectable curves, pause/resume, finish, reset, and repeating alternate
playback. Its editable CSS endpoints are `assets/animation-start.css` and
`assets/animation-end.css`. The demo uses its complete dark theme underneath
these examples.

The Animation CTest covers timing, pause/seek/resume, delayed and immediate
completion, alternate/infinite repetition, interpolation, replacement, deletion,
callback mutation/errors, CSS parse failure, mixed units, input focus, animated
hit testing, explicit size contracts, inherited colors, and layout-cache reuse.
GpuIntegration captures actual C++ renderer start/middle/end frames, compares
framebuffer pixels with computed colors and geometry, and activates a translated
button in the synchronized property group.
