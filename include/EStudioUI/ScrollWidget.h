#pragma once

#include "FontFace.h"
#include "Widget.h"

#include <list>
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
    virtual void SetScrollOffset(Point offset);

    //! Returns the current offset clamped against current geometry/model size.
    Point GetScrollOffset() const;

    //! Returns the viewport excluding reserved scrollbar/header space.
    virtual Rect GetViewport() const;

    //! Returns a visible scrollbar track in layout coordinates, or an empty rectangle.
    Rect GetScrollTrackBounds(bool horizontal) const;

    //! Returns a visible scrollbar thumb in layout coordinates, or an empty rectangle.
    Rect GetScrollThumbBounds(bool horizontal) const;

    //! Returns the number of retained shaped captions, bounded to 512 entries.
    std::size_t GetTextCacheEntryCount() const;

    //! Returns estimated retained caption bytes, bounded to eight MiB.
    std::size_t GetTextCacheBytes() const;

  protected:
    //! Retires cached shaping when registered font resources change.
    void OnFontsChanged() noexcept override;

    //! Returns stored scroll coordinates without recursively querying virtual geometry.
    Point GetStoredScrollOffset() const;

    //! Returns the virtual content dimensions.
    virtual Point GetVirtualSize() const = 0;

    //! Brings a virtual-coordinate rectangle into the viewport.
    void EnsureVisible(Rect rectangle);

    //! Draws a cached, vertically centered single-line caption.
    void DrawRowText(Renderer& renderer, FontFace& font, std::string_view text, Rect rectangle, Color color,
                     Rect clip) const;

    //! Draws a part caption; opacity includes both owner and part opacity, like Renderer::DrawBox.
    void DrawRowText(Renderer& renderer, FontFace& font, std::string_view text, Rect rectangle, const Style& style,
                     Rect clip, float opacity = 1) const;

    //! Returns a cached shaped caption; an oversized caption uses one replaceable scratch run.
    const TextLayout& TextFor(FontFace& font, std::string_view text, const Style& style) const;

    //! Shapes a caption at an explicit size with the widget's inherited typography.
    const TextLayout& TextFor(FontFace& font, std::string_view text, float size) const;

    //! Returns orientation and current pointer/drag state for one scrollbar paint part.
    unsigned ScrollPartState(bool horizontal, bool thumb) const;

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
    //! Resolves the reserved scrollbar width or height from its orientation style.
    float ScrollGutter(bool horizontal) const;

    //! Returns the track's usable interior after border and padding.
    Rect ScrollTrackContent(bool horizontal) const;

    //! Refreshes scrollbar hover bookkeeping without resolving selectors.
    void UpdateScrollHover(const PointerEvent& event);

    //! Resolves a stationary window pointer against current transformed scrollbar geometry.
    void RefreshScrollHover() const;

    //! Updates cached hover from a layout point and reports whether its state changed.
    bool RecomputeScrollHover(Point position) const;

    //! Non-owning lookup avoids allocating strings while repainting cached rows.
    struct TextLookup
    {
        //! UTF-8 caption bytes.
        std::string_view m_Text;

        //! Font service that supplied glyph identifiers.
        const FontFace* m_Font = nullptr;

        //! Requested logical-pixel font size.
        float m_FontSize = 0;

        //! Lifetime-unique font service identity.
        std::uint64_t m_Identity = 0;
        //! Complete registration revision.
        std::uint64_t m_Revision = 0;
        //! Exact requested CSS families.
        std::string_view m_Family;
        //! Requested CSS weight.
        float m_Weight = 400;
        //! Requested CSS font style.
        std::string_view m_Style;
        //! Requested paragraph direction.
        std::string_view m_Direction;
        //! Requested shaping language.
        std::string_view m_Language;
    };

    //! A shaped caption identity includes the font service and exact font size.
    struct TextKey
    {
        //! UTF-8 caption bytes.
        std::string m_Text;

        //! Font service that supplied glyph identifiers.
        const FontFace* m_Font = nullptr;

        //! Requested logical-pixel font size.
        float m_FontSize = 0;

        //! Lifetime-unique font service identity.
        std::uint64_t m_Identity = 0;
        //! Complete registration revision.
        std::uint64_t m_Revision = 0;
        //! Exact requested CSS families.
        std::string m_Family;
        //! Requested CSS weight.
        float m_Weight = 400;
        //! Requested CSS font style.
        std::string m_Style;
        //! Requested paragraph direction.
        std::string m_Direction;
        //! Requested shaping language.
        std::string m_Language;

        //! Compares every shaping input.
        bool operator==(const TextKey&) const = default;

        //! Compares a temporary lookup without copying its caption.
        bool operator==(const TextLookup& other) const;
    };

    //! Hashes the complete shaped caption identity.
    struct TextHash
    {
        //! Enables allocation-free heterogeneous lookup.
        using is_transparent = void;

        //! Combines caption, font identity, and size hashes.
        std::size_t operator()(const TextKey& key) const;

        //! Hashes the same identity without owning caption bytes.
        std::size_t operator()(const TextLookup& key) const;
    };

    //! A shaped run and its bounded-cache accounting.
    struct TextEntry
    {
        //! Cached glyph positions and metrics.
        TextLayout m_Layout;

        //! Estimated retained storage including duplicated LRU keys.
        std::size_t m_Bytes = 0;

        //! Position in the most-recently-used ordering.
        std::list<TextKey>::iterator m_Recency;
    };

    //! Stores the requested scroll position.
    Point m_Offset;

    //! Records the active drag axis: zero, horizontal, or vertical.
    int m_DragAxis = 0;

    //! Stores the pointer coordinate at drag start.
    float m_DragOrigin = 0;

    //! Stores the scroll coordinate at drag start.
    float m_OffsetOrigin = 0;

    //! Records the hovered scrollbar axis: zero, horizontal, or vertical.
    mutable int m_HoverAxis = 0;

    //! Records whether the pointer is over that axis's thumb.
    mutable bool m_HoverThumb = false;

    //! Detects pointer moves delivered to an embedded child instead of this control.
    mutable std::optional<Point> m_HoverPointer;

    //! Caches at most 512 recently drawn strings.
    mutable std::unordered_map<TextKey, TextEntry, TextHash, std::equal_to<>> m_TextCache;

    //! Orders retained captions from newest to oldest.
    mutable std::list<TextKey> m_TextRecency;

    //! Counts estimated bytes retained by the caption cache.
    mutable std::size_t m_TextBytes = 0;

    //! Stores one uncached caption exceeding the retention budget.
    mutable TextLayout m_TextScratch;
};
} // namespace EStudioUI
