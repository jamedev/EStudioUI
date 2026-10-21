#pragma once

#include "FontFace.h"
#include "Widget.h"

#include <functional>
#include <unordered_map>

namespace EStudioUI
{
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

    //! Bubbles a right-click or Menu-key request through the hit ancestor path.
    void ContextMenu(Point position);

    //! Shows one owned overlay, clamps it to the window, and saves keyboard focus.
    void ShowPopup(Widget& popup, Point anchor);

    //! Dismisses the active overlay and restores the previous live focus target.
    void ClosePopup();

    //! Returns the live active popup, or null.
    Widget* GetPopup() const;

    //! Prepares external GPU surfaces while the window's GL context is current.
    void PrepareSurfaces(double time, float scaleX, float scaleY);

    //! Records surface-consumer fences after Renderer::EndFrame.
    void FrameSubmitted();

    //! Returns whether visible surfaces request continuous animation.
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

    //! Sets the current monotonic time used by caret animation.
    void SetTime(double time);

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
    //! Recursively resolves inherited and state-dependent CSS.
    void Restyle(Widget& widget, const Style* parent);

    //! Computes intrinsic and explicit widget dimensions.
    Point Measure(Widget& widget, float parentWidth, float parentHeight);

    //! Places children using the documented flex subset.
    void Layout(Widget& widget, Rect bounds, Rect clip);

    //! Finds the frontmost displayed widget inside all ancestor clips.
    Widget* HitTest(Widget& widget, Point position) const;

    //! Draws a subtree in painter order with inherited opacity.
    void PaintTree(Widget& widget, Renderer& renderer, double time, float opacity);

    //! Advances keyboard focus in tree order.
    void FocusNext(bool backwards);

    //! Observes the font service, which outlives the context.
    FontFace& m_Font;

    //! Owns the window's root and descendants.
    Widget m_Root{"window"};

    //! Stores the last valid CSS rules.
    StyleSheet m_StyleSheet;

    //! Observes the focus target safely across callback-driven destruction.
    WidgetRef m_Focus;

    //! Observes the widget receiving captured pointer events.
    WidgetRef m_Capture;

    //! Observes the previous pointer ancestor path.
    std::vector<WidgetRef> m_HoverPath;

    //! Caches the logical window width.
    float m_Width = 1;

    //! Caches the logical window height.
    float m_Height = 1;

    //! Marks style and layout as invalid.
    bool m_Dirty = true;

    //! Marks the framebuffer as needing repaint.
    bool m_Repaint = true;

    //! Stores monotonic event time.
    double m_Time = 0;

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
