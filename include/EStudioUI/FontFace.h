#pragma once

#include "Types.h"
#include "FontSelection.h"
#include "ThreadAffinity.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace EStudioUI
{
struct Style;
class UiContext;

namespace Detail
{
struct FontResource;
}

//! One explicitly registered outline font and its CSS matching metadata.
struct FontSource
{
    //! Font file, including TrueType/OpenType collections.
    std::filesystem::path m_Path;
    //! CSS family alias; an empty alias uses the font's family name.
    std::string m_Family;
    //! CSS weight; variable fonts receive this wght coordinate.
    int m_Weight = 400;
    //! Matches italic/oblique requests to an actual italic font file.
    bool m_Italic = false;
    //! Face index within a font collection.
    int m_FaceIndex = 0;
};

//! Font and text resource admission limits, including resources retained by old layouts.
struct FontLimits
{
    //! Maximum simultaneously live native faces.
    std::size_t m_MaxFaces = 32;
    //! Maximum aggregate loaded font-file bytes.
    std::size_t m_MaxFontBytes = 128 * 1024 * 1024;
    //! Maximum aggregate cached CPU outline bytes.
    std::size_t m_MaxGeometryBytes = 16 * 1024 * 1024;
    //! Maximum shared shaping-cache bytes; oversized results are not admitted.
    std::size_t m_MaxShapeBytes = 4 * 1024 * 1024;
    //! Maximum shared shaping-cache entries.
    std::size_t m_MaxShapeEntries = 256;
};

//! Owned font-service counters; reading them performs no shaping or glyph loading.
struct FontStats
{
    //! Currently registered font faces.
    std::size_t m_RegisteredFaces = 0;
    //! Native faces retained by either registration or live text layouts.
    std::size_t m_LiveFaces = 0;
    //! Loaded font-file storage, including retired faces.
    std::size_t m_FontBytes = 0;
    //! Cached CPU outlines across live faces.
    std::size_t m_GeometryBytes = 0;
    //! Shared shaping-cache storage.
    std::size_t m_ShapeBytes = 0;
    //! Shared shaping-cache entries.
    std::size_t m_ShapeEntries = 0;
    //! Actual native shaping operations.
    std::size_t m_Shapes = 0;
    //! Exact shared shaping-cache hits.
    std::size_t m_CacheHits = 0;
};

//! A logical byte boundary and its affinity at a bidirectional run boundary.
struct TextCaret
{
    //! UTF-8 grapheme boundary.
    std::size_t m_Offset = 0;
    //! Uses the preceding grapheme's edge rather than the following edge.
    bool m_Trailing = false;
    //! Compares logical location and affinity.
    bool operator==(const TextCaret&) const = default;
};

//! One visual caret stop and its logical identity.
struct VisualCaret
{
    //! Logical offset and affinity.
    TextCaret m_Caret;
    //! Horizontal coordinate in logical pixels.
    float m_X = 0;
};

//! One grapheme's logical range and visual horizontal extent.
struct TextSpan
{
    //! Inclusive logical UTF-8 boundary.
    std::size_t m_First = 0;
    //! Exclusive logical UTF-8 boundary.
    std::size_t m_Last = 0;
    //! Visual left edge.
    float m_Left = 0;
    //! Visual right edge.
    float m_Right = 0;
    //! Maps the logical start to the visual right edge.
    bool m_RightToLeft = false;
};

//! A quadratic glyph outline segment expressed in em units.
struct GlyphCurve
{
    //! Initial control point.
    Point m_P0;

    //! Intermediate control point.
    Point m_P1;

    //! Final control point.
    Point m_P2;
};

//! Resolution-independent glyph geometry cached on the CPU.
struct GlyphGeometry
{
    //! Quadratic outline segments, with straight edges encoded as quadratics.
    std::vector<GlyphCurve> m_Curves;

    //! Left outline bound in em units.
    float m_MinX = 0;

    //! Bottom outline bound in em units.
    float m_MinY = 0;

    //! Right outline bound in em units.
    float m_MaxX = 0;

    //! Top outline bound in em units.
    float m_MaxY = 0;
};

//! One positioned glyph with its actual source font.
struct PositionedGlyph
{
    //! Font-local glyph identifier.
    std::uint32_t m_Id = 0;

    //! Horizontal origin relative to the run start.
    float m_X = 0;

    //! Vertical origin offset, positive upwards.
    float m_Y = 0;

    //! Index in the layout's retained source-font array.
    std::uint32_t m_FontSlot = 0;
};

//! Cached mixed-script shaping, visual caret stops and logical selection geometry.
struct TextLayout
{
    //! Positioned glyph sequence.
    std::vector<PositionedGlyph> m_Glyphs;

    //! UTF-8 grapheme boundaries corresponding to caret locations.
    std::vector<std::size_t> m_Offsets;

    //! Logical-pixel caret positions for each grapheme boundary.
    std::vector<float> m_Carets;

    //! Sorted visual caret stops, including split boundary affinities.
    std::vector<VisualCaret> m_VisualCarets;

    //! Grapheme extents used for disjoint mixed-direction selections.
    std::vector<TextSpan> m_Spans;

    //! Unicode word boundaries for visual word navigation.
    std::vector<std::size_t> m_WordOffsets;

    //! Keeps actual fonts alive after registry removal or replacement.
    std::vector<std::shared_ptr<Detail::FontResource>> m_Fonts;

    //! Exact shaping request.
    FontSelection m_Selection;

    //! Lifetime-unique originating font service identity.
    std::uint64_t m_Service = 0;

    //! Originating font registration revision.
    std::uint64_t m_Revision = 0;

    //! Resolved paragraph direction.
    bool m_RightToLeft = false;

    //! Number of unsupported glyphs after the registered fallback chain.
    std::size_t m_MissingGlyphs = 0;

    //! Run advance width.
    float m_Width = 0;

    //! Font ascender above the baseline.
    float m_Ascender = 0;

    //! Font descender below the baseline.
    float m_Descender = 0;

    //! Requested font size in logical pixels.
    float m_FontSize = 16;

    //! Finds the nearest caret to a horizontal position.
    std::size_t HitTest(float x) const;

    //! Returns the horizontal location of a UTF-8 caret boundary.
    float CaretPosition(std::size_t offset) const;

    //! Resolves a visual hit without discarding bidirectional affinity.
    TextCaret HitTestCaret(float x) const;

    //! Resolves a logical caret and its affinity.
    float CaretPosition(TextCaret caret) const;

    //! Moves to the adjacent visual grapheme or Unicode word boundary.
    TextCaret MoveCaret(TextCaret caret, int direction, bool word = false) const;

    //! Returns the visual start/end of the resolved paragraph direction.
    TextCaret EdgeCaret(bool end) const;

    //! Returns merged local rectangles for a contiguous logical selection.
    std::vector<Rect> SelectionRectangles(std::size_t first, std::size_t last) const;

    //! Returns the actual lifetime-unique font identity of a glyph slot.
    std::uint64_t GetFontIdentity(std::uint32_t slot) const;

    //! Loads the glyph from its retained actual font on the font owner thread.
    const GlyphGeometry& GetGlyph(const PositionedGlyph& glyph) const;

    //! Returns the owned vector/string storage used by this layout.
    std::size_t StorageBytes() const;
};

//! Loads a font once and supplies shaping and outline data independently of GL.
class FontFace
{
  public:
    //! Opens a TrueType or OpenType font, throwing a diagnostic on failure.
    explicit FontFace(const std::filesystem::path& path, FontLimits limits = {});

    //! Releases shaping and font resources.
    ~FontFace();

    //! Font faces own their native resources.
    FontFace(const FontFace&) = delete;

    //! Font faces own their native resources.
    FontFace& operator=(const FontFace&) = delete;

    //! Shapes one UTF-8 control paragraph with automatic direction and fallback.
    TextLayout Shape(std::string_view text, float size, bool editable = false) const;

    //! Shapes using inherited CSS typography; editable runs disable optional ligatures.
    TextLayout Shape(std::string_view text, const Style& style, bool editable = false) const;

    //! Shapes with a size override while preserving the supplied CSS font selection.
    TextLayout Shape(std::string_view text, float size, const Style& style, bool editable = false) const;

    //! Wraps one display paragraph using Unicode breaks and paragraph-level bidi state.
    std::vector<TextLayout> Wrap(std::string_view text, const Style& style, float width) const;

    //! Tests all CSS typography and registration inputs without shaping or allocation.
    bool Matches(const TextLayout& layout, const Style& style) const;

    //! Registers a face as a CSS candidate and ordered fallback, returning its identity.
    std::uint64_t AddFont(FontSource source);

    //! Retires a nonprimary registration; existing layouts retain its native resources.
    bool RemoveFont(std::uint64_t identity);

    //! Returns registered family names in deterministic fallback order.
    std::vector<std::string> GetFamilies() const;

    //! Returns the lifetime-unique primary face identity.
    std::uint64_t GetIdentity() const;

    //! Returns the current registration revision.
    std::uint64_t GetRevision() const;

    //! Returns font, outline and shaping-cache accounting.
    FontStats GetStats() const;

    //! Retires shared shaping and CPU outline caches without expiring live layouts.
    void ClearCaches();

    //! Returns a primary-font outline; the reference expires on later cache eviction/retirement.
    const GlyphGeometry& GetGlyph(std::uint32_t id);

    //! Rejects access from another thread before touching font or shaping state.
    void VerifyAccess() const;

  private:
    //! Lets contexts bind registration guards and automatic font retirement.
    friend class UiContext;
    //! Adds one owner-thread context using this service.
    void AttachHost(UiContext& context);
    //! Removes one context before its widget storage is released.
    void DetachHost(UiContext& context) noexcept;
    //! Rejects registration changes during any attached UI traversal.
    void VerifyMutation() const;
    //! Retires dependent UI caches and invalidates their geometry.
    void NotifyHosts();

    //! Keeps each native font/shaping service on its creating thread.
    ThreadAffinity m_Affinity;

    //! Owns registrations, retained-resource accounting and bounded shaping caches.
    struct Impl;

    //! Hides third-party types from the public interface.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
