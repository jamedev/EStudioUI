#pragma once

#include "Types.h"
#include "ThreadAffinity.h"

#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace EStudioUI
{
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

//! One positioned glyph in a shaped left-to-right run.
struct PositionedGlyph
{
    //! Font-local glyph identifier.
    std::uint32_t m_Id = 0;

    //! Horizontal origin relative to the run start.
    float m_X = 0;

    //! Vertical origin offset, positive upwards.
    float m_Y = 0;
};

//! Cached shaping and caret geometry for one left-to-right text run.
struct TextLayout
{
    //! Positioned glyph sequence.
    std::vector<PositionedGlyph> m_Glyphs;

    //! UTF-8 grapheme boundaries corresponding to caret locations.
    std::vector<std::size_t> m_Offsets;

    //! Logical-pixel caret positions for each grapheme boundary.
    std::vector<float> m_Carets;

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
};

//! Loads a font once and supplies shaping and outline data independently of GL.
class FontFace
{
  public:
    //! Opens a TrueType or OpenType font, throwing a diagnostic on failure.
    explicit FontFace(const std::filesystem::path& path);

    //! Releases shaping and font resources.
    ~FontFace();

    //! Font faces own their native resources.
    FontFace(const FontFace&) = delete;

    //! Font faces own their native resources.
    FontFace& operator=(const FontFace&) = delete;

    //! Shapes one UTF-8 LTR run; editable runs disable common/contextual ligatures.
    TextLayout Shape(std::string_view text, float size, bool editable = false) const;

    //! Returns cached unhinted outline geometry for a glyph.
    const GlyphGeometry& GetGlyph(std::uint32_t id);

    //! Rejects access from another thread before touching font or shaping state.
    void VerifyAccess() const;

  private:
    //! Keeps each native font/shaping service on its creating thread.
    ThreadAffinity m_Affinity;

    //! Owns FreeType, HarfBuzz, font bytes, and the glyph cache.
    struct Impl;

    //! Hides third-party types from the public interface.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
