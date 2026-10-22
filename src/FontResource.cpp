#include "FontResource.h"
#include <EStudioUI/Style.h>

#include FT_OUTLINE_H
#include FT_MULTIPLE_MASTERS_H
#include <hb-ot.h>
#include <utf8proc.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <iterator>
#include <limits>
#include <tuple>
#include <stdexcept>

namespace EStudioUI::Detail
{
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
    std::exception_ptr m_Error;

    void Append(GlyphCurve curve)
    {
        if (m_Glyph.m_Curves.size() >= 16384)
            throw std::length_error("One glyph exceeds the 16,384-curve budget.");
        m_Glyph.m_Curves.push_back(curve);
    }

    Point Convert(const FT_Vector* value) const
    {
        return {static_cast<float>(value->x) * m_Scale, static_cast<float>(value->y) * m_Scale};
    }

    void Line(Point end)
    {
        if (m_Glyph.m_Curves.size() >= 16384)
            throw std::length_error("One glyph exceeds the 16,384-curve budget.");
        if (m_Current.m_X != end.m_X || m_Current.m_Y != end.m_Y)
            Append({m_Current, end, end});
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
            Append({p0, control, p3});
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
        try
        {
            self.Close();
            self.m_Current = self.Convert(point);
            self.m_Start = self.m_Current;
            self.m_Open = true;
        }
        catch (...)
        {
            self.m_Error = std::current_exception();
            return 1;
        }
        return 0;
    }

    static int LineTo(const FT_Vector* point, void* user)
    {
        auto& self = *static_cast<OutlineBuilder*>(user);
        try
        {
            self.Line(self.Convert(point));
        }
        catch (...)
        {
            self.m_Error = std::current_exception();
            return 1;
        }
        return 0;
    }

    static int ConicTo(const FT_Vector* control, const FT_Vector* point, void* user)
    {
        auto& self = *static_cast<OutlineBuilder*>(user);
        try
        {
            const auto end = self.Convert(point);
            self.Append({self.m_Current, self.Convert(control), end});
            self.m_Current = end;
        }
        catch (...)
        {
            self.m_Error = std::current_exception();
            return 1;
        }
        return 0;
    }

    static int CubicTo(const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* point, void* user)
    {
        auto& self = *static_cast<OutlineBuilder*>(user);
        try
        {
            const auto end = self.Convert(point);
            self.Cubic(self.m_Current, self.Convert(c1), self.Convert(c2), end, 0);
            self.m_Current = end;
        }
        catch (...)
        {
            self.m_Error = std::current_exception();
            return 1;
        }
        return 0;
    }
};
} // namespace

namespace
{
std::atomic<std::uint64_t> s_NextFont{1};

std::string Lower(std::string text)
{
    for (auto& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    return text;
}

std::string Family(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n\"'");
    if (first == std::string_view::npos)
        return {};
    const auto last = value.find_last_not_of(" \t\r\n\"'");
    return Lower(std::string(value.substr(first, last - first + 1)));
}
} // namespace

FontResource::FontResource(FontSource source, std::shared_ptr<FontBudget> budget)
    : m_Id(s_NextFont.fetch_add(1)), m_Source(std::move(source)), m_Budget(std::move(budget))
{
    try
    {
        if (m_Source.m_Weight < 1 || m_Source.m_Weight > 1000 || m_Source.m_FaceIndex < 0 ||
            m_Source.m_Family.size() > 256 || !std::filesystem::is_regular_file(m_Source.m_Path))
            throw std::invalid_argument("A font requires a regular file, valid weight/index and bounded family name.");
        const auto bytes = std::filesystem::file_size(m_Source.m_Path);
        if (!bytes || bytes > 64 * 1024 * 1024 || bytes > m_Budget->m_Limits.m_MaxFontBytes ||
            m_Budget->m_FontBytes.load() > m_Budget->m_Limits.m_MaxFontBytes - bytes ||
            m_Budget->m_Faces.load() >= m_Budget->m_Limits.m_MaxFaces)
            throw std::length_error(
                "Font admission exceeds the configured file/face budget, including retained layouts.");
        std::ifstream input(m_Source.m_Path, std::ios::binary);
        m_Bytes.resize(static_cast<std::size_t>(bytes));
        if (!input.read(m_Bytes.data(), static_cast<std::streamsize>(bytes)))
            throw std::runtime_error("Cannot read complete font: " + m_Source.m_Path.string());
        if (FT_Init_FreeType(&m_Library) ||
            FT_New_Memory_Face(m_Library, reinterpret_cast<const FT_Byte*>(m_Bytes.data()),
                               static_cast<FT_Long>(m_Bytes.size()), m_Source.m_FaceIndex, &m_Face) ||
            !m_Face->units_per_EM || !FT_IS_SCALABLE(m_Face))
            throw std::runtime_error("FreeType requires a valid scalable outline face.");
        if (m_Source.m_Family.empty())
            m_Source.m_Family = m_Face->family_name ? m_Face->family_name : "sans-serif";
        FT_MM_Var* axes = nullptr;
        if (!FT_Get_MM_Var(m_Face, &axes))
        {
            const auto release = [this](FT_MM_Var* value) { FT_Done_MM_Var(m_Library, value); };
            const std::unique_ptr<FT_MM_Var, decltype(release)> owned(axes, release);
            std::vector<FT_Fixed> coordinates(axes->num_axis);
            for (FT_UInt i = 0; i < axes->num_axis; ++i)
                coordinates[i] = axes->axis[i].tag == FT_MAKE_TAG('w', 'g', 'h', 't')
                                     ? std::clamp<FT_Fixed>(static_cast<FT_Fixed>(m_Source.m_Weight) * 65536,
                                                            axes->axis[i].minimum, axes->axis[i].maximum)
                                     : axes->axis[i].def;
            if (FT_Set_Var_Design_Coordinates(m_Face, axes->num_axis, coordinates.data()))
                throw std::runtime_error("FreeType could not apply the requested font variation.");
        }
        m_Blob = hb_blob_create(m_Bytes.data(), static_cast<unsigned>(m_Bytes.size()), HB_MEMORY_MODE_READONLY, nullptr,
                                nullptr);
        m_HbFace = hb_face_create(m_Blob, static_cast<unsigned>(m_Source.m_FaceIndex));
        m_Font = hb_font_create(m_HbFace);
        hb_ot_font_set_funcs(m_Font);
        hb_font_set_scale(m_Font, m_Face->units_per_EM, m_Face->units_per_EM);
        const hb_variation_t weight{HB_TAG('w', 'g', 'h', 't'), static_cast<float>(m_Source.m_Weight)};
        hb_font_set_variations(m_Font, &weight, 1);
        m_Budget->m_FontBytes += m_Bytes.size();
        ++m_Budget->m_Faces;
        m_Counted = true;
    }
    catch (...)
    {
        Release();
        throw;
    }
}

void FontResource::Release() noexcept
{
    ClearGeometry();
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
    if (m_Counted)
    {
        m_Budget->m_FontBytes -= m_Bytes.size();
        --m_Budget->m_Faces;
    }
}

FontResource::~FontResource()
{
    Release();
}

bool FontResource::Covers(std::string_view grapheme) const
{
    for (std::size_t at = 0; at < grapheme.size();)
    {
        utf8proc_int32_t point = 0;
        const auto count = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(grapheme.data() + at),
                                            static_cast<utf8proc_ssize_t>(grapheme.size() - at), &point);
        if (count <= 0)
            return false;
        at += static_cast<std::size_t>(count);
        if (utf8proc_category(point) == UTF8PROC_CATEGORY_CF || (point >= 0xFE00 && point <= 0xFE0F) ||
            (point >= 0xE0100 && point <= 0xE01EF))
            continue;
        hb_codepoint_t glyph = 0;
        if (!hb_font_get_nominal_glyph(m_Font, static_cast<hb_codepoint_t>(point), &glyph) || !glyph)
            return false;
    }
    return true;
}

void FontResource::ClearGeometry()
{
    for (const auto& [id, entry] : m_Glyphs)
    {
        (void)id;
        m_Budget->m_GeometryBytes -= entry.m_Bytes;
    }
    m_Glyphs.clear();
    m_Recency.clear();
}

const GlyphGeometry& FontResource::Glyph(std::uint32_t id)
{
    m_Affinity.VerifyAccess("FontResource::Glyph");
    if (auto found = m_Glyphs.find(id); found != m_Glyphs.end())
    {
        found->second.m_Stamp = ++m_Budget->m_Tick;
        m_Recency.splice(m_Recency.begin(), m_Recency, found->second.m_Recency);
        return found->second.m_Geometry;
    }
    GlyphGeometry geometry;
    if (FT_Load_Glyph(m_Face, id, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP))
        throw std::runtime_error("Cannot load glyph " + std::to_string(id));
    const auto slot = m_Face->glyph;
    if (slot->format == FT_GLYPH_FORMAT_OUTLINE)
    {
        const float scale = 1.0f / static_cast<float>(m_Face->units_per_EM);
        FT_BBox bounds{};
        FT_Outline_Get_CBox(&slot->outline, &bounds);
        geometry.m_MinX = static_cast<float>(bounds.xMin) * scale;
        geometry.m_MinY = static_cast<float>(bounds.yMin) * scale;
        geometry.m_MaxX = static_cast<float>(bounds.xMax) * scale;
        geometry.m_MaxY = static_cast<float>(bounds.yMax) * scale;
        OutlineBuilder builder{geometry, scale, {}, {}, false, {}};
        const FT_Outline_Funcs functions{
            OutlineBuilder::MoveTo, OutlineBuilder::LineTo, OutlineBuilder::ConicTo, OutlineBuilder::CubicTo, 0, 0};
        const auto failed = FT_Outline_Decompose(&slot->outline, &functions, &builder);
        if (builder.m_Error)
            std::rethrow_exception(builder.m_Error);
        if (failed)
            throw std::runtime_error("Invalid glyph outline.");
        builder.Close();
    }
    const auto bytes = sizeof(GlyphEntry) + 64 + geometry.m_Curves.capacity() * sizeof(GlyphCurve);
    if (bytes > m_Budget->m_Limits.m_MaxGeometryBytes)
        throw std::length_error("One glyph exceeds the configured CPU geometry budget.");
    while (m_Budget->m_GeometryBytes.load() + bytes > m_Budget->m_Limits.m_MaxGeometryBytes || m_Glyphs.size() >= 4096)
    {
        std::shared_ptr<FontResource> victim;
        std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
        for (const auto& weak : m_Budget->m_Resources)
            if (const auto face = weak.lock(); face && !face->m_Recency.empty())
            {
                const auto stamp = face->m_Glyphs.at(face->m_Recency.back()).m_Stamp;
                if ((m_Glyphs.size() >= 4096 && face.get() != this) || stamp >= oldest)
                    continue;
                victim = face;
                oldest = stamp;
            }
        if (!victim)
            throw std::length_error("CPU glyph geometry budget cannot admit this outline.");
        const auto key = victim->m_Recency.back();
        m_Budget->m_GeometryBytes -= victim->m_Glyphs.at(key).m_Bytes;
        victim->m_Glyphs.erase(key);
        victim->m_Recency.pop_back();
    }
    m_Recency.push_front(id);
    try
    {
        const auto entry =
            m_Glyphs.emplace(id, GlyphEntry{std::move(geometry), bytes, m_Recency.begin(), ++m_Budget->m_Tick});
        m_Budget->m_GeometryBytes += bytes;
        return entry.first->second.m_Geometry;
    }
    catch (...)
    {
        m_Recency.pop_front();
        throw;
    }
}

FontSelection SelectFont(const Style& style, float size)
{
    return {style.m_FontFamily, size, style.m_FontWeight, style.m_FontStyle, style.m_TextDirection, style.m_Language};
}

void ValidateControlText(std::string_view text)
{
    if (text.size() > 1048576)
        throw std::length_error("One control text paragraph is limited to 1 MiB of UTF-8.");
    for (std::size_t at = 0; at < text.size();)
    {
        utf8proc_int32_t point = 0;
        const auto count = utf8proc_iterate(reinterpret_cast<const utf8proc_uint8_t*>(text.data() + at),
                                            static_cast<utf8proc_ssize_t>(text.size() - at), &point);
        if (count <= 0 || point == 0 || point < 32 || point == 0x85 || point == 0x2028 || point == 0x2029)
            throw std::invalid_argument("Shaping requires valid single-paragraph UTF-8 without embedded NULs.");
        at += static_cast<std::size_t>(count);
    }
}

std::vector<std::shared_ptr<FontResource>> ResolveFonts(const std::vector<std::shared_ptr<FontResource>>& fonts,
                                                        const FontSelection& selection)
{
    std::vector<std::string> families;
    std::size_t start = 0;
    char quote = 0;
    for (std::size_t i = 0; i <= selection.m_Family.size(); ++i)
    {
        const char c = i == selection.m_Family.size() ? ',' : selection.m_Family[i];
        if ((c == '\'' || c == '"') && (!quote || quote == c))
            quote = quote ? 0 : c;
        else if (c == ',' && !quote)
        {
            families.push_back(Family(std::string_view(selection.m_Family).substr(start, i - start)));
            start = i + 1;
        }
    }
    if (quote || families.empty() || families.size() > 16 ||
        std::any_of(families.begin(), families.end(), [](const auto& family) { return family.empty(); }))
        throw std::invalid_argument("A CSS font-family list requires one to sixteen balanced family names.");
    auto result = fonts;
    const auto rank = [&](const std::shared_ptr<FontResource>& font)
    {
        const auto family = Family(font->m_Source.m_Family);
        std::size_t familyRank = families.size() + 1;
        for (std::size_t i = 0; i < families.size(); ++i)
            if (family == families[i] || ((families[i] == "sans-serif" || families[i] == "serif" ||
                                           families[i] == "monospace" || families[i] == "system-ui") &&
                                          family == Family(fonts.front()->m_Source.m_Family)))
            {
                familyRank = i;
                break;
            }
        return std::tuple{familyRank, font->m_Source.m_Italic != (selection.m_Style != "normal"),
                          std::abs(static_cast<float>(font->m_Source.m_Weight) - selection.m_Weight)};
    };
    std::stable_sort(result.begin(), result.end(), [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
    return result;
}
} // namespace EStudioUI::Detail
