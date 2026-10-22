#pragma once

#include "FontFace.h"
#include "Widget.h"
#include "AnimationSystem.h"

#include <functional>
#include <unordered_map>

namespace EStudioUI
{
class CssMotion;

//! Cumulative counters used to verify cache behavior across animation frames.
struct UiStats
{
    //! Number of computed widget or decorative styles.
    std::size_t m_StyleResolutions = 0;

    //! Number of complete measurement and arrangement passes.
    std::size_t m_LayoutPasses = 0;
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

    //! Returns the root widget.
    Widget& Root();

    //! Replaces CSS atomically, retaining the previous theme on error.
    bool SetStyleSheet(std::string_view css, std::string& error);

    //! Loads a UTF-8 stylesheet from disk.
    bool LoadStyleSheet(const std::filesystem::path& path, std::string& error);

    //! Returns the window-local property animation scheduler.
    AnimationSystem& Animations();

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
    void Restyle(Widget& widget, const Style* parent);

    //! Computes intrinsic and explicit widget dimensions.
    Point Measure(Widget& widget, float parentWidth, float parentHeight);

    //! Measures and arranges the current computed styles without changing input ownership.
    void Arrange();

    //! Places children using the documented flex subset.
    void Layout(Widget& widget, Rect bounds, Rect clip);

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

    //! Marks style and layout as invalid.
    bool m_Dirty = true;

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

    //! Caches one bottom-up measurement per visible node and prepare pass.
    std::unordered_map<Widget*, Point> m_Measurements;

    //! Records explicit main-axis overflow for layout diagnostics.
    std::vector<WidgetRef> m_OverflowingContainers;
};
} // namespace EStudioUI
