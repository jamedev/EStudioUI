#pragma once

#include "CanvasScene.h"
#include "Widget.h"

#include <array>
#include <limits>

namespace EStudioUI
{
//! A camera-controlled, BVH-culled view of lightweight scene items.
class CanvasWidget : public Widget
{
  public:
    //! Creates an empty scene with a unit-scale camera at the world origin.
    CanvasWidget();

    //! Shares a scene model; null creates a new empty model.
    void SetScene(std::shared_ptr<CanvasScene> scene);

    //! Returns the model; call SceneChanged after modifying it directly.
    CanvasScene& GetScene();

    //! Returns read-only scene data.
    const CanvasScene& GetScene() const;

    //! Requests a frame after direct model changes.
    void SceneChanged();

    //! Converts logical window coordinates into double-precision world coordinates.
    CanvasPoint ToWorld(Point point) const;

    //! Converts world coordinates relative to the camera before narrowing to float.
    Point ToScreen(CanvasPoint point) const;

    //! Returns the world-space camera center.
    CanvasPoint GetCenter() const;

    //! Returns logical pixels per world unit.
    double GetZoom() const;

    //! Changes the camera, clamping zoom to [1e-6, 1e6] and rejecting nonfinite values.
    void SetCamera(CanvasPoint center, double zoom);

    //! Frames all model bounds with a logical-pixel margin.
    void FitScene(double margin = 30);

    //! Returns the content viewport in world coordinates.
    CanvasBounds GetViewportWorldBounds() const;

    //! Enables the adaptive grid and world axes.
    void SetGridVisible(bool visible);

    //! Makes ordinary left-button drags pan, alongside Alt/Space drags.
    void SetPanMode(bool enabled);

    //! Returns whether the current captured gesture is a camera pan.
    bool IsPanning() const;

    //! Adds a shared CSS identity and returns its palette index.
    std::uint32_t RegisterItemStyle(std::string type, std::vector<std::string> classes = {}, std::string id = {});

    //! Compiles static item paint styles atomically, including :selected variants.
    bool SetItemStyleSheet(std::string_view css, std::string& error);

    //! Replaces temporary geometry drawn above scene items.
    void SetPreview(std::vector<CanvasGeometry> geometry, std::uint32_t style = 0);

    //! Displays or clears a world-space selection marquee.
    void SetSelectionRectangle(std::optional<CanvasBounds> bounds);

    //! Owns a sparse regular widget anchored to a world-space rectangle.
    Widget& HostWidget(std::unique_ptr<Widget> widget, CanvasBounds bounds);

    //! Removes an owned host and its scene anchor.
    bool RemoveHostedWidget(Widget& widget);

    //! Returns the number of broad-phase visible items in the current view.
    std::size_t GetVisibleItemCount() const;

    //! Returns BVH nodes visited by the latest viewport query.
    std::size_t GetQueryVisitCount() const;

    //! Reports camera changes after invalidation and hosted-widget placement.
    Signal<>& ViewChanged();

    //! Allows keyboard shortcuts while the canvas owns focus.
    bool IsFocusable() const override;

  protected:
    //! Draws the grid, visible model items, and temporary overlays.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

    //! Begins a camera pan when the current mode or modifiers request it.
    void OnPointerDown(const PointerEvent& event) override;

    //! Tracks the pointer and updates a captured pan.
    void OnPointerMove(const PointerEvent& event) override;

    //! Ends a camera pan.
    void OnPointerUp(const PointerEvent& event) override;

    //! Resets gesture state after capture cancellation.
    void OnCaptureLost() override;

    //! Tracks Space as a temporary pan modifier.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Clears temporary pan state when focus leaves the canvas.
    void OnFocus(bool focused) override;

    //! Places sparse widget hosts before child layout.
    void OnLayout() override;

    //! Zooms around the wheel event position, or the center before pointer input.
    bool OnScroll(Point delta, Modifiers modifiers) override;

  private:
    //! One CSS identity with precomputed normal and selected paint values.
    struct PaletteEntry
    {
        //! Selector identity without a persistent ancestor pointer.
        StyleNode m_Node;

        //! Normal and selected presentations.
        std::array<Style, 2> m_Styles;
    };

    //! A lifetime-safe observer and world rectangle for a regular widget.
    struct HostedWidget
    {
        //! Observer invalidated when the child is removed independently.
        WidgetRef m_Widget;

        //! World-space placement.
        CanvasBounds m_Bounds;
    };

    //! Recomputes the small palette outside paint and input loops.
    void CompilePalette();

    //! Refreshes the cached viewport query only when model or camera data changes.
    void UpdateVisibleItems() const;

    //! Draws a primitive after camera-relative conversion and line clipping.
    void DrawGeometry(Renderer& renderer, const CanvasGeometry& geometry, const Style& style, Rect clip,
                      float opacity) const;

    //! Draws a bounded adaptive grid using screen-space strokes.
    void DrawGrid(Renderer& renderer, Rect clip, float opacity) const;

    //! Updates world-anchored floating rectangles without changing visibility contracts.
    void PlaceHostedWidgets();

    //! Owns or shares the scene data.
    std::shared_ptr<CanvasScene> m_Scene;

    //! World-space camera center.
    CanvasPoint m_Center;

    //! Logical pixels per world unit.
    double m_Zoom = 1;

    //! Enables the adaptive background grid.
    bool m_GridVisible = true;

    //! Enables persistent pan mode.
    bool m_PanMode = false;

    //! Tracks the Space pan modifier.
    bool m_SpaceDown = false;

    //! Tracks a captured camera gesture.
    bool m_Panning = false;

    //! Last logical pointer location for wheel anchoring.
    std::optional<Point> m_LastPointer;

    //! Pointer position at the start of a pan.
    Point m_PanStart;

    //! Camera position at the start of a pan.
    CanvasPoint m_PanCenter;

    //! Compiled item stylesheet, independent of the widget-tree stylesheet.
    StyleSheet m_ItemStyleSheet;

    //! Shared styles; items store only one integer index.
    std::vector<PaletteEntry> m_Palette;

    //! Conservative logical-pixel reach of any palette stroke or outer shadow.
    double m_PaintMargin = 2;

    //! Scene revision represented by the cached BVH query.
    mutable std::uint64_t m_CachedRevision = std::numeric_limits<std::uint64_t>::max();

    //! Cached world query rectangle, including paint overflow.
    mutable CanvasBounds m_CachedQuery;

    //! Reused visible-ID scratch storage in stable painter order.
    mutable std::vector<CanvasItemId> m_VisibleItems;

    //! BVH visit count for the cached query.
    mutable std::size_t m_QueryVisits = 0;

    //! Temporary geometry that never enters the persistent model or BVH.
    std::vector<CanvasGeometry> m_Preview;

    //! Shared style used by temporary geometry.
    std::uint32_t m_PreviewStyle = 0;

    //! Optional world-space selection overlay.
    std::optional<CanvasBounds> m_SelectionRectangle;

    //! Small set of fully interactive widget children.
    std::vector<HostedWidget> m_Hosts;

    //! Camera-change subscribers.
    Signal<> m_ViewChanged;
};
} // namespace EStudioUI
