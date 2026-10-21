#pragma once

#include "FontFace.h"
#include "Widget.h"

#include <unordered_map>

namespace EStudioUI
{
//! Shared wheel, draggable scrollbar, clipping, and bounded text-cache behavior.
class ScrollWidget : public Widget
{
  public:
    //! Creates a scrollable control with the supplied CSS type.
    explicit ScrollWidget(std::string type);

    //! Sets a clamped logical-pixel scroll position.
    void SetScrollOffset(Point offset);

    //! Returns the current offset clamped against current geometry/model size.
    Point GetScrollOffset() const;

    //! Returns the viewport excluding reserved scrollbar/header space.
    virtual Rect GetViewport() const;

  protected:
    //! Returns the virtual content dimensions.
    virtual Point GetVirtualSize() const = 0;

    //! Brings a virtual-coordinate rectangle into the viewport.
    void EnsureVisible(Rect rectangle);

    //! Draws a cached, vertically centered single-line caption.
    void DrawRowText(Renderer& renderer, FontFace& font, std::string_view text, Rect rectangle, Color color,
                     Rect clip) const;

    //! Draws scrollbars after row content.
    void PaintScrollBars(Renderer& renderer, float opacity) const;

    //! Starts a scrollbar interaction and returns whether it consumed the press.
    bool HandleScrollPointerDown(const PointerEvent& event);

    //! Updates an active scrollbar drag.
    void OnPointerMove(const PointerEvent& event) override;

    //! Ends a scrollbar drag.
    void OnPointerUp(const PointerEvent& event) override;

    //! Cancels a scrollbar drag.
    void OnCaptureLost() override;

    //! Scrolls by wheel notches; Shift routes vertical wheels horizontally.
    bool OnScroll(Point delta, Modifiers modifiers) override;

  private:
    //! Computes a draggable scrollbar thumb, or an empty rectangle.
    Rect ScrollThumb(bool horizontal) const;

    //! Stores the requested scroll position.
    Point m_Offset;

    //! Records the active drag axis: zero, horizontal, or vertical.
    int m_DragAxis = 0;

    //! Stores the pointer coordinate at drag start.
    float m_DragOrigin = 0;

    //! Stores the scroll coordinate at drag start.
    float m_OffsetOrigin = 0;

    //! Caches at most 512 recently drawn strings.
    mutable std::unordered_map<std::string, TextLayout> m_TextCache;

    //! Records the font size used by cached runs.
    mutable float m_CacheFontSize = -1;
};
} // namespace EStudioUI
