#pragma once

#include "FontFace.h"
#include "Widget.h"
#include "AnimationSystem.h"
#include "UiHandle.h"
#include "StateStore.h"

#include <functional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace EStudioUI
{
class CssMotion;

//! Identifies traversals during which synchronous structural changes are unsafe.
enum class UiPhase
{
    //! Allows ordinary input, commands, and deferred commits.
    Idle,
    //! Resolves cached widget and part styles.
    Style,
    //! Computes intrinsic geometry.
    Measure,
    //! Arranges widgets and invokes their layout hooks.
    Layout,
    //! Prepares external scene surfaces before painting.
    SurfacePreparation,
    //! Records the ordered widget drawing commands.
    Paint,
    //! Notifies surfaces that their shared textures were consumed.
    Submission,
    //! Queries extension hooks without permitting changes to the traversed tree.
    Inspection
};

//! Cumulative counters used to verify cache behavior across animation frames.
struct UiStats
{
    //! Number of computed widget or decorative styles.
    std::size_t m_StyleResolutions = 0;

    //! Number of layout updates begun after geometry invalidation.
    std::size_t m_LayoutPasses = 0;

    //! Number of intrinsic measurement hooks invoked by the layout engine.
    std::size_t m_Measurements = 0;

    //! Number of aggregate widget measurements computed for actual constraints.
    std::size_t m_MeasureCalculations = 0;

    //! Number of aggregate measurements reused for unchanged constraints.
    std::size_t m_MeasureCacheHits = 0;

    //! Number of widgets whose arrangement and layout hook executed.
    std::size_t m_Arrangements = 0;

    //! Number of pure constraint-aware content hooks invoked after cache misses.
    std::size_t m_ConstrainedMeasurements = 0;

    //! Number of container strategy computations, including natural-size queries.
    std::size_t m_LayoutPlans = 0;
};

//! Owns one window's widget tree, style resolution, layout, and input routing.
class UiContext
{
  public:
    //! Creates a root container using the supplied font service.
    explicit UiContext(FontFace& font);

    //! Disconnects context links before destruction.
    ~UiContext();

    //! A context owns one stable widget tree.
    UiContext(const UiContext&) = delete;

    //! A context owns one stable widget tree.
    UiContext& operator=(const UiContext&) = delete;

    //! Throws before direct access from a thread other than the owner.
    void VerifyAccess() const;

    //! Returns whether the current thread owns this context.
    bool IsOwnerThread() const noexcept;

    //! Returns a copyable endpoint that producers may use without touching UI state.
    UiDispatcher GetDispatcher() const;

    //! Captures a typed target that only the owning dispatcher resolves into a widget.
    template <typename T> UiHandle<T> GetHandle(T& target) const
    {
        static_assert(std::is_base_of_v<Widget, T> && !std::is_const_v<T>,
                      "A UI handle requires a mutable Widget-derived target.");
        VerifyAccess();
        auto& widget = static_cast<Widget&>(target);
        widget.VerifyAccess();
        if (widget.GetContext() != this)
            throw std::invalid_argument("A UI handle target must belong to this context.");
        const WidgetRef reference(&widget);
        const auto* owner = this;
        return UiHandle<T>::ForTarget(
            m_Dispatcher, widget.m_TargetKey,
            [reference, owner]() -> T*
            {
                auto* live = reference.Get();
                return live && live->GetContext() == owner ? dynamic_cast<T*>(live) : nullptr;
            },
            widget.m_LifetimeStop.get_token());
    }

    //! Queues a context-owned commit for a later dispatcher drain, outside current traversal.
    DispatchReceipt Defer(std::function<void(UiContext&)> callback);

    //! Queues a move-capable callable accepting either the live context or no arguments.
    template <typename Callback>
        requires(!std::is_same_v<std::decay_t<Callback>, std::function<void(UiContext&)>>)
    DispatchReceipt Defer(Callback&& callback)
    {
        VerifyAccess();
        using Function = std::decay_t<Callback>;
        static_assert(std::is_invocable_v<Function&, UiContext&> || std::is_invocable_v<Function&>,
                      "A deferred callback must accept UiContext& or no arguments.");
        auto work = std::make_shared<Function>(std::forward<Callback>(callback));
        return Defer(std::function<void(UiContext&)>(
            [work = std::move(work)](UiContext& context)
            {
                if constexpr (std::is_invocable_v<Function&, UiContext&>)
                    std::invoke(*work, context);
                else
                    std::invoke(*work);
            }));
    }

    //! Returns the current owner-thread traversal phase for diagnostics.
    UiPhase GetPhase() const;

    //! Returns the root widget.
    Widget& Root();

    //! Replaces CSS atomically, retaining the previous theme on error.
    bool SetStyleSheet(std::string_view css, std::string& error);

    //! Loads a UTF-8 stylesheet from disk.
    bool LoadStyleSheet(const std::filesystem::path& path, std::string& error);

    //! Returns the window-local property animation scheduler.
    AnimationSystem& Animations();

    //! Returns the lazily created observable state domain owned by this context.
    StateStore& States();

    //! Animates existing widgets from current presentation to a parsed final stylesheet.
    AnimationId AnimateStyleSheet(std::string_view finalCss, const AnimationOptions& options, std::string& error);

    //! Parses both endpoints before animating existing widgets between complete stylesheets.
    AnimationId AnimateStyleSheet(std::string_view initialCss, std::string_view finalCss,
                                  const AnimationOptions& options, std::string& error);

    //! Invalidates style/layout caches and requests repaint.
    void Invalidate();

    //! Requests a frame while retaining style and layout caches.
    void RequestRepaint();

    //! Returns whether an input or model change requires a frame.
    bool NeedsRepaint() const;

    //! Resolves styles and layout against current logical window dimensions.
    void Prepare(float width, float height);

    //! Paints the prepared tree and clears the repaint request.
    void Paint(Renderer& renderer, double time);

    //! Delivers a pointer move, maintaining ancestor hover states.
    void PointerMove(Point position, Modifiers modifiers = {});

    //! Starts left-button capture and assigns keyboard focus.
    void PointerDown(Point position, Modifiers modifiers = {}, int clicks = 1);

    //! Releases capture even when the pointer is outside the pressed widget.
    void PointerUp(Point position, Modifiers modifiers = {});

    //! Routes wheel deltas to the nearest scrollable ancestor.
    void Scroll(Point position, Point delta, Modifiers modifiers = {});

    //! Returns the latest pointer or wheel position in logical window coordinates.
    std::optional<Point> GetPointerPosition() const;

    //! Bubbles a right-click or Menu-key request through the hit ancestor path.
    void ContextMenu(Point position);

    //! Shows one owned overlay, clamps it to the window, and saves keyboard focus.
    void ShowPopup(Widget& popup, Point anchor);

    //! Owns a root popup until its logical owner is removed; expired popups are pruned before layout.
    Widget& AddOwnedPopup(std::unique_ptr<Widget> popup, Widget& owner);

    //! Dismisses the active overlay and restores the previous live focus target.
    void ClosePopup();

    //! Returns the live active popup, or null.
    Widget* GetPopup() const;

    //! Prepares external GPU surfaces while the window's GL context is current.
    void PrepareSurfaces(double time, float scaleX, float scaleY);

    //! Records surface-consumer fences after Renderer::EndFrame.
    void FrameSubmitted();

    //! Returns whether property timelines or visible surfaces request animation frames.
    bool HasAnimations() const;

    //! Reports containers whose fixed/content bases exceed their available space.
    const std::vector<WidgetRef>& GetOverflowingContainers() const;

    //! Copies the last prepared issues of visible containers; explicitly visits the visible widget tree.
    std::vector<LayoutDiagnostic> GetLayoutDiagnostics() const;

    //! Dispatches a keyboard event or performs Tab navigation.
    void KeyEvent(Key key, bool down, bool repeat = false, Modifiers modifiers = {});

    //! Delivers committed UTF-8 text to the focused control.
    void TextInput(std::string_view text);

    //! Sets keyboard focus, rejecting disabled or nonfocusable nodes.
    void SetFocus(Widget* widget);

    //! Returns the live focused widget.
    Widget* GetFocus() const;

    //! Cancels capture and focus after window deactivation.
    void Deactivate();

    //! Returns the font service shared by controls and the renderer.
    FontFace& GetFont() const;

    //! Advances property timelines and the caret clock before layout and painting.
    void SetTime(double time);

    //! Returns cumulative style and layout work.
    const UiStats& GetStats() const;

    //! Converts window input coordinates into a widget's untransformed layout coordinates.
    Point ToLayoutPoint(const Widget& widget, Point point) const;

    //! Returns current monotonic GUI time.
    double GetTime() const;

    //! Connects platform clipboard services.
    void SetClipboard(std::function<std::string()> get, std::function<void(std::string_view)> set);

    //! Reads Unicode clipboard text, or an empty string when unavailable.
    std::string GetClipboard() const;

    //! Writes Unicode clipboard text through the platform service.
    void SetClipboardText(std::string_view text) const;

    //! Returns a safe observer for delayed GUI-thread callbacks.
    WidgetRef Observe(Widget* widget) const;

  private:
    friend class Widget;
    friend class CssMotion;
    friend class Window;
    friend class AnimationSystem;

    //! Associates a containing window without creating a context ownership cycle.
    void SetOwnerLifetime(std::weak_ptr<void> owner);

    //! Keeps the containing window alive until a callback-driving operation returns.
    std::shared_ptr<void> RetainOwner() const;

    //! Observes the native owner; standalone contexts need no external lifetime lease.
    std::weak_ptr<void> m_OwnerLifetime;

    //! Restores the previous phase even when a user hook throws.
    struct PhaseScope;

    //! Rejects structural changes until the current traversal ends.
    void VerifyStructuralMutation() const;

    //! Rejects nested public traversals and dispatcher drains during a frame phase.
    void VerifyDispatchPhase() const;

    //! Prevents frame traversal from observing staged values or incomplete binding updates.
    void VerifyTraversalEntry() const;

    //! Propagates a widget edit through its style and geometry dependencies.
    void Invalidate(Widget& widget, Invalidation flags);

    //! Captures this context's owner independently of object lifetime.
    ThreadAffinity m_Affinity;

    //! Owns a synchronized queue that closes before widget/native teardown.
    UiDispatcher m_Dispatcher;

    //! Owns observable state independently of widgets and expires it during context teardown.
    std::unique_ptr<StateStore> m_States;

    //! Tracks guarded traversals without changing ordinary input callback semantics.
    mutable UiPhase m_Phase = UiPhase::Idle;

    //! Refreshes cached part defaults after paint-only owner changes without matching selectors.
    void RefreshStyleParts(Widget& widget);

    //! Applies bounded shared-palette presentation overrides above CSS part motion.
    void ApplyPartOverrides(Widget& widget);

    //! Clears logical selector links before an owner is destroyed.
    void ReleaseSelectorOwner(const Widget& owner);

    //! Removes popups whose composite-widget owners have been destroyed.
    void PruneOwnedPopups();

    //! Keeps popup ownership independent of composite-widget layout and clipping.
    struct OwnedPopup
    {
        //! Observes the composite widget that requested the popup.
        WidgetRef m_Owner;

        //! Observes the popup owned directly by the root.
        WidgetRef m_Popup;
    };

    //! Tracks popup lifetimes without extending their logical owners' lifetimes.
    std::vector<OwnedPopup> m_OwnedPopups;

    //! Recursively resolves inherited and state-dependent CSS.
    void Restyle(Widget& widget, const Style* parent, bool force = false);

    //! Computes intrinsic and explicit widget dimensions.
    MeasureResult Measure(Widget& widget, MeasureConstraints parent, MeasureConstraints allocation = {},
                          bool definiteWidth = false, bool definiteHeight = false);

    //! Builds and validates a pure strategy plan against the current displayed children.
    LayoutPlan Plan(Widget& widget, MeasureConstraints content, bool definiteWidth, bool definiteHeight);

    //! Returns descriptors for displayed, in-flow children in ownership order.
    std::vector<Widget*> LayoutChildren(Widget& widget) const;

    //! Measures and arranges the current computed styles without changing input ownership.
    void Arrange();

    //! Places children using the documented flex subset.
    void Layout(Widget& widget, Rect bounds, Rect clip, bool definiteWidth = false, bool definiteHeight = false);

    //! Finds the frontmost displayed widget inside all ancestor clips.
    Widget* HitTest(Widget& widget, Point position, Affine parentTransform = {}) const;

    //! Maps pointer coordinates and preserves world-space ancestor clipping during capture.
    PointerEvent MakePointerEvent(const Widget& widget, Point position, Modifiers modifiers = {}, int clicks = 1) const;

    //! Refreshes hover after geometry changes without synthesizing pointer callbacks.
    bool RefreshHover();

    //! Draws a subtree in painter order with inherited opacity.
    void PaintTree(Widget& widget, Renderer& renderer, double time, float opacity);

    //! Advances keyboard focus in tree order.
    void FocusNext(bool backwards);

    //! Captures endpoint layout once and builds temporary computed-style tracks.
    AnimationId BeginStyleTransition(std::optional<StyleSheet> initial, StyleSheet final,
                                     const AnimationOptions& options);

    //! Observes the font service, which outlives the context.
    FontFace& m_Font;

    //! Owns the window's root and descendants.
    Widget m_Root{"window"};

    //! Stores the last valid CSS rules.
    StyleSheet m_StyleSheet;

    //! Owns GUI-thread timelines, destroyed before the widget tree.
    AnimationSystem m_Animations;

    //! Owns compiled CSS timelines and lightweight decorative presentations.
    std::unique_ptr<CssMotion> m_CssMotion;

    //! Cumulative cache instrumentation.
    UiStats m_Stats;

    //! Identifies the current whole-stylesheet transition for replacement.
    AnimationId m_StyleTransition = 0;

    //! Prevents synchronous completion listeners from overwriting a newer transition identity.
    std::uint64_t m_StyleTransitionGeneration = 0;

    //! Observes the focus target safely across callback-driven destruction.
    WidgetRef m_Focus;

    //! Observes the widget receiving captured pointer events.
    WidgetRef m_Capture;

    //! Observes the previous pointer ancestor path.
    std::vector<WidgetRef> m_HoverPath;

    //! Remembers the stationary pointer for animated geometry and resize hover updates.
    std::optional<Point> m_PointerPosition;

    //! Caches the logical window width.
    float m_Width = 1;

    //! Caches the logical window height.
    float m_Height = 1;

    //! Marks pending style, geometry, or input dependency work.
    bool m_Dirty = true;

    //! Requests conservative selector resolution without eagerly walking the tree.
    bool m_FullStyleDirty = true;

    //! Preserves the legacy whole-tree intrinsic invalidation escape hatch.
    bool m_ResetMeasurements = true;

    //! Requests input geometry refresh without invalidating CSS or layout.
    bool m_HitTestDirty = true;

    //! Marks the framebuffer as needing repaint.
    bool m_Repaint = true;

    //! Reads platform clipboard text.
    std::function<std::string()> m_GetClipboard;

    //! Writes platform clipboard text.
    std::function<void(std::string_view)> m_SetClipboard;

    //! Observes the active overlay without extending its lifetime.
    WidgetRef m_Popup;

    //! Saves focus for restoration when a popup closes.
    WidgetRef m_PopupReturnFocus;

    //! Stores the requested popup origin in logical window coordinates.
    Point m_PopupAnchor;

    //! Records explicit main-axis overflow for layout diagnostics.
    std::vector<WidgetRef> m_OverflowingContainers;
};
} // namespace EStudioUI
