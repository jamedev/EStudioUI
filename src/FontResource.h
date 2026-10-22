#pragma once

#include <EStudioUI/FontFace.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

#include <atomic>
#include <list>
#include <optional>
#include <unordered_map>

namespace EStudioUI::Detail
{
//! Shared admission ledger outliving registrations retained by text layouts.
struct FontBudget
{
    //! Configured hard font and cache limits.
    FontLimits m_Limits;
    //! Weak inventory for global outline LRU eviction.
    std::vector<std::weak_ptr<FontResource>> m_Resources;
    //! Monotonic owner-side outline access sequence.
    std::uint64_t m_Tick = 0;
    //! Counts registered and retired native resources.
    std::atomic<std::size_t> m_Faces{0};
    //! Counts file bytes until the final retained face is destroyed.
    std::atomic<std::size_t> m_FontBytes{0};
    //! Counts cached native-outline storage.
    std::atomic<std::size_t> m_GeometryBytes{0};
};

//! An actual native face; a layout retains it independently of CSS registrations.
struct FontResource
{
    //! Loads one bounded font file and applies its declared variable weight.
    FontResource(FontSource source, std::shared_ptr<FontBudget> budget);
    //! Releases native resources and returns their admission accounting.
    ~FontResource();
    //! Releases initialized handles after construction failure or destruction.
    void Release() noexcept;
    //! Returns an LRU-cached outline with a bounded lifetime until later cache mutation.
    const GlyphGeometry& Glyph(std::uint32_t id);
    //! Retires cached outlines while preserving the loaded font.
    void ClearGeometry();
    //! Tests coverage of every nonformat code point in one grapheme.
    bool Covers(std::string_view grapheme) const;

    //! Keeps mutable FreeType outline work on its owner.
    ThreadAffinity m_Affinity;
    //! Lifetime-unique cache identity.
    std::uint64_t m_Id = 0;
    //! Exact registered matching metadata.
    FontSource m_Source;
    //! Shared aggregate resource accounting.
    std::shared_ptr<FontBudget> m_Budget;
    //! Owns bounded font-file bytes.
    std::vector<char> m_Bytes;
    //! Native outline service.
    FT_Library m_Library = nullptr;
    //! Actual font face, including collection index and variation coordinates.
    FT_Face m_Face = nullptr;
    //! Immutable HarfBuzz byte owner.
    hb_blob_t* m_Blob = nullptr;
    //! HarfBuzz font face.
    hb_face_t* m_HbFace = nullptr;
    //! Shaping instance with the same variation coordinates as FreeType.
    hb_font_t* m_Font = nullptr;
    //! Records whether aggregate accounting has been acquired.
    bool m_Counted = false;

    //! An owned outline and its LRU/accounting metadata.
    struct GlyphEntry
    {
        //! Analytic outline curves.
        GlyphGeometry m_Geometry;
        //! Accounted entry storage.
        std::size_t m_Bytes = 0;
        //! Position in the access-order list.
        std::list<std::uint32_t>::iterator m_Recency;
        //! Last global access sequence.
        std::uint64_t m_Stamp = 0;
    };

    //! Cached glyphs for this exact font instance.
    std::unordered_map<std::uint32_t, GlyphEntry> m_Glyphs;
    //! Most recently accessed glyph first.
    std::list<std::uint32_t> m_Recency;
};

//! Captures only the inherited fields that influence shaping.
FontSelection SelectFont(const Style& style, float size);

//! Validates the complete single-paragraph UTF-8 shaping boundary.
void ValidateControlText(std::string_view text);

//! Orders matching CSS faces followed by deterministic registered fallbacks.
std::vector<std::shared_ptr<FontResource>> ResolveFonts(const std::vector<std::shared_ptr<FontResource>>& fonts,
                                                        const FontSelection& selection);

//! Shapes a full paragraph or wraps it using its original embedding levels.
std::vector<TextLayout> ShapeControlText(std::string_view text, const FontSelection& selection, bool editable,
                                         const std::vector<std::shared_ptr<FontResource>>& fonts,
                                         std::optional<float> width = {});
} // namespace EStudioUI::Detail
