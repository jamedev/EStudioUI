#include <EStudioUI/FontFace.h>
#include <EStudioUI/TextBuffer.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <hb.h>
#include <hb-ot.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace EStudioUI
{
struct FontFace::Impl
{
    FT_Library m_Library = nullptr;
    FT_Face m_Face = nullptr;
    hb_blob_t* m_Blob = nullptr;
    hb_face_t* m_HbFace = nullptr;
    hb_font_t* m_Font = nullptr;
    std::vector<char> m_Bytes;
    std::unordered_map<std::uint32_t, GlyphGeometry> m_Glyphs;

    ~Impl()
    {
        if (m_Font)
            hb_font_destroy(m_Font);
        if (m_HbFace)
            hb_face_destroy(m_HbFace);
        if (m_Blob)
            hb_blob_destroy(m_Blob);
        if (m_Face)
            FT_Done_Face(m_Face);
        if (m_Library)
            FT_Done_FreeType(m_Library);
    }
};

namespace
{
Point Mid(Point a, Point b)
{
    return {(a.m_X + b.m_X) * 0.5f, (a.m_Y + b.m_Y) * 0.5f};
}

struct OutlineBuilder
{
    GlyphGeometry& m_Glyph;
    float m_Scale;
    Point m_Current;
    Point m_Start;
    bool m_Open = false;

    Point Convert(const FT_Vector* value) const
    {
        return {static_cast<float>(value->x) * m_Scale, static_cast<float>(value->y) * m_Scale};
    }

    void Line(Point end)
    {
        if (m_Current.m_X != end.m_X || m_Current.m_Y != end.m_Y)
            m_Glyph.m_Curves.push_back({m_Current, end, end});
        m_Current = end;
    }

    void Close()
    {
        if (m_Open)
            Line(m_Start);
    }

    void Cubic(Point p0, Point p1, Point p2, Point p3, int depth)
    {
        const Point control{(3 * (p1.m_X + p2.m_X) - p0.m_X - p3.m_X) * 0.25f,
                            (3 * (p1.m_Y + p2.m_Y) - p0.m_Y - p3.m_Y) * 0.25f};
        const Point c1{(p0.m_X + 2 * control.m_X) / 3, (p0.m_Y + 2 * control.m_Y) / 3};
        const Point c2{(p3.m_X + 2 * control.m_X) / 3, (p3.m_Y + 2 * control.m_Y) / 3};
        const float error =
            std::max(std::hypot(c1.m_X - p1.m_X, c1.m_Y - p1.m_Y), std::hypot(c2.m_X - p2.m_X, c2.m_Y - p2.m_Y));
        if (error <= 0.00005f || depth == 12)
            m_Glyph.m_Curves.push_back({p0, control, p3});
        else
        {
            const auto p01 = Mid(p0, p1);
            const auto p12 = Mid(p1, p2);
            const auto p23 = Mid(p2, p3);
            const auto p012 = Mid(p01, p12);
            const auto p123 = Mid(p12, p23);
            const auto center = Mid(p012, p123);
            Cubic(p0, p01, p012, center, depth + 1);
            Cubic(center, p123, p23, p3, depth + 1);
        }
    }

    static int MoveTo(const FT_Vector* point, void* user)
    {
        auto& self = *static_cast<OutlineBuilder*>(user);
        self.Close();
        self.m_Current = self.Convert(point);
        self.m_Start = self.m_Current;
        self.m_Open = true;
        return 0;
    }

    static int LineTo(const FT_Vector* point, void* user)
    {
        auto& self = *static_cast<OutlineBuilder*>(user);
        self.Line(self.Convert(point));
        return 0;
    }

    static int ConicTo(const FT_Vector* control, const FT_Vector* point, void* user)
    {
        auto& self = *static_cast<OutlineBuilder*>(user);
        const auto end = self.Convert(point);
        self.m_Glyph.m_Curves.push_back({self.m_Current, self.Convert(control), end});
        self.m_Current = end;
        return 0;
    }

    static int CubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* point, void* user)
    {
        auto& self = *static_cast<OutlineBuilder*>(user);
        const auto end = self.Convert(point);
        self.Cubic(self.m_Current, self.Convert(c1), self.Convert(c2), end, 0);
        self.m_Current = end;
        return 0;
    }
};
} // namespace

FontFace::FontFace(const std::filesystem::path& path) : m_Impl(std::make_unique<Impl>())
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read font: " + path.string());
    m_Impl->m_Bytes.assign(std::istreambuf_iterator<char>(input), {});
    if (m_Impl->m_Bytes.empty() || m_Impl->m_Bytes.size() > 67108864)
        throw std::runtime_error("Font size must be between 1 byte and 64 MiB.");
    if (FT_Init_FreeType(&m_Impl->m_Library) != 0 ||
        FT_New_Memory_Face(m_Impl->m_Library, reinterpret_cast<const FT_Byte*>(m_Impl->m_Bytes.data()),
                           static_cast<FT_Long>(m_Impl->m_Bytes.size()), 0, &m_Impl->m_Face) != 0)
        throw std::runtime_error("FreeType could not load the font.");
    if (m_Impl->m_Face->units_per_EM == 0)
        throw std::runtime_error("An outline font with a valid em square is required.");
    m_Impl->m_Blob = hb_blob_create(m_Impl->m_Bytes.data(), static_cast<unsigned>(m_Impl->m_Bytes.size()),
                                    HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    m_Impl->m_HbFace = hb_face_create(m_Impl->m_Blob, 0);
    m_Impl->m_Font = hb_font_create(m_Impl->m_HbFace);
    hb_ot_font_set_funcs(m_Impl->m_Font);
    const auto units = m_Impl->m_Face->units_per_EM;
    hb_font_set_scale(m_Impl->m_Font, units, units);
}

FontFace::~FontFace() = default;

TextLayout FontFace::Shape(std::string_view text, float size, bool editable) const
{
    if (!std::isfinite(size) || size < 0 || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::invalid_argument("Invalid font size or text run length.");
    TextLayout layout;
    layout.m_FontSize = size;
    const float scale = size / static_cast<float>(m_Impl->m_Face->units_per_EM);
    layout.m_Ascender = static_cast<float>(m_Impl->m_Face->ascender) * scale;
    layout.m_Descender = -static_cast<float>(m_Impl->m_Face->descender) * scale;
    layout.m_Offsets = TextBuffer::Graphemes(text);
    layout.m_Carets.resize(layout.m_Offsets.size(), 0);
    if (text.empty())
        return layout;

    std::unique_ptr<hb_buffer_t, decltype(&hb_buffer_destroy)> buffer(hb_buffer_create(), hb_buffer_destroy);
    hb_buffer_add_utf8(buffer.get(), text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_set_direction(buffer.get(), HB_DIRECTION_LTR);
    hb_buffer_set_cluster_level(buffer.get(), HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
    hb_buffer_guess_segment_properties(buffer.get());
    hb_feature_t features[2]{};
    hb_feature_from_string("liga=0", -1, &features[0]);
    hb_feature_from_string("clig=0", -1, &features[1]);
    hb_shape(m_Impl->m_Font, buffer.get(), editable ? features : nullptr, editable ? 2u : 0u);
    unsigned count = 0;
    const auto* info = hb_buffer_get_glyph_infos(buffer.get(), &count);
    const auto* positions = hb_buffer_get_glyph_positions(buffer.get(), &count);
    std::vector<std::pair<std::size_t, float>> clusters;
    for (unsigned i = 0; i < count; ++i)
    {
        if (clusters.empty() || clusters.back().first != info[i].cluster)
            clusters.emplace_back(info[i].cluster, layout.m_Width);
        layout.m_Glyphs.push_back({info[i].codepoint,
                                   layout.m_Width + static_cast<float>(positions[i].x_offset) * scale,
                                   static_cast<float>(positions[i].y_offset) * scale});
        layout.m_Width += static_cast<float>(positions[i].x_advance) * scale;
    }
    clusters.emplace_back(text.size(), layout.m_Width);
    std::size_t cluster = 0;
    for (std::size_t i = 0; i < layout.m_Offsets.size(); ++i)
    {
        const auto offset = layout.m_Offsets[i];
        while (cluster + 1 < clusters.size() && clusters[cluster + 1].first <= offset)
            ++cluster;
        float x = clusters[cluster].second;
        if (cluster + 1 < clusters.size() && offset > clusters[cluster].first)
        {
            const auto first =
                std::lower_bound(layout.m_Offsets.begin(), layout.m_Offsets.end(), clusters[cluster].first);
            const auto last =
                std::lower_bound(layout.m_Offsets.begin(), layout.m_Offsets.end(), clusters[cluster + 1].first);
            const float fraction =
                static_cast<float>(static_cast<std::ptrdiff_t>(i) - (first - layout.m_Offsets.begin())) /
                static_cast<float>(std::max<std::ptrdiff_t>(1, last - first));
            x += (clusters[cluster + 1].second - x) * fraction;
        }
        layout.m_Carets[i] = x;
    }
    return layout;
}

const GlyphGeometry& FontFace::GetGlyph(std::uint32_t id)
{
    if (const auto found = m_Impl->m_Glyphs.find(id); found != m_Impl->m_Glyphs.end())
        return found->second;
    GlyphGeometry geometry;
    if (FT_Load_Glyph(m_Impl->m_Face, id, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0)
        throw std::runtime_error("Cannot load glyph " + std::to_string(id));
    const auto slot = m_Impl->m_Face->glyph;
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE)
    {
        const float scale = 1.0f / static_cast<float>(m_Impl->m_Face->units_per_EM);
        FT_BBox bounds{};
        FT_Outline_Get_CBox(&slot->outline, &bounds);
        geometry.m_MinX = static_cast<float>(bounds.xMin) * scale;
        geometry.m_MinY = static_cast<float>(bounds.yMin) * scale;
        geometry.m_MaxX = static_cast<float>(bounds.xMax) * scale;
        geometry.m_MaxY = static_cast<float>(bounds.yMax) * scale;
        OutlineBuilder builder{geometry, scale, {}, {}, false};
        const FT_Outline_Funcs functions{
            OutlineBuilder::MoveTo, OutlineBuilder::LineTo, OutlineBuilder::ConicTo, OutlineBuilder::CubicTo, 0, 0};
        if (FT_Outline_Decompose(&slot->outline, &functions, &builder) != 0)
            throw std::runtime_error("Invalid glyph outline.");
        builder.Close();
    }
    return m_Impl->m_Glyphs.emplace(id, std::move(geometry)).first->second;
}

std::size_t TextLayout::HitTest(float x) const
{
    if (m_Carets.empty())
        return 0;
    for (std::size_t i = 1; i < m_Carets.size(); ++i)
        if (x < (m_Carets[i - 1] + m_Carets[i]) * 0.5f)
            return m_Offsets[i - 1];
    return m_Offsets.back();
}

float TextLayout::CaretPosition(std::size_t offset) const
{
    if (m_Carets.empty())
        return 0;
    const auto end = std::upper_bound(m_Offsets.begin(), m_Offsets.end(), offset);
    const auto index = end == m_Offsets.begin() ? 0 : static_cast<std::size_t>(std::prev(end) - m_Offsets.begin());
    return m_Carets[index];
}
} // namespace EStudioUI
