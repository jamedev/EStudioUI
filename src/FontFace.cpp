#include <EStudioUI/FontFace.h>
#include <EStudioUI/Style.h>
#include <EStudioUI/UiContext.h>
#include "FontResource.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <list>
#include <stdexcept>
#include <unordered_map>

namespace EStudioUI
{
namespace
{
struct ShapeKey
{
    std::string m_Text;
    FontSelection m_Font;
    bool m_Editable = false;
    bool operator==(const ShapeKey&) const = default;
};

struct ShapeLookup
{
    std::string_view m_Text;
    const FontSelection& m_Font;
    bool m_Editable = false;
};

struct ShapeHash
{
    using is_transparent = void;

    std::size_t operator()(const ShapeLookup& key) const
    {
        auto result = std::hash<std::string_view>{}(key.m_Text);
        const auto add = [&](std::size_t value) { result ^= value + 0x9e3779b9U + (result << 6) + (result >> 2); };
        add(std::hash<std::string>{}(key.m_Font.m_Family));
        add(std::hash<std::string>{}(key.m_Font.m_Style));
        add(std::hash<std::string>{}(key.m_Font.m_Direction));
        add(std::hash<std::string>{}(key.m_Font.m_Language));
        add(std::hash<float>{}(key.m_Font.m_Size));
        add(std::hash<float>{}(key.m_Font.m_Weight));
        add(key.m_Editable ? 1 : 0);
        return result;
    }

    std::size_t operator()(const ShapeKey& key) const
    {
        return (*this)(ShapeLookup{key.m_Text, key.m_Font, key.m_Editable});
    }
};

struct ShapeEqual
{
    using is_transparent = void;

    bool operator()(const ShapeKey& a, const ShapeKey& b) const
    {
        return a == b;
    }

    bool operator()(const ShapeKey& a, const ShapeLookup& b) const
    {
        return a.m_Text == b.m_Text && a.m_Font == b.m_Font && a.m_Editable == b.m_Editable;
    }

    bool operator()(const ShapeLookup& a, const ShapeKey& b) const
    {
        return (*this)(b, a);
    }
};
} // namespace

struct FontFace::Impl
{
    std::shared_ptr<Detail::FontBudget> m_Budget;
    std::vector<std::shared_ptr<Detail::FontResource>> m_Faces;
    std::uint64_t m_Revision = 1;
    std::vector<UiContext*> m_Hosts;

    struct Entry
    {
        TextLayout m_Layout;
        std::size_t m_Bytes = 0;
        std::list<ShapeKey>::iterator m_Recency;
    };

    mutable std::unordered_map<ShapeKey, Entry, ShapeHash, ShapeEqual> m_Cache;
    mutable std::list<ShapeKey> m_Recency;
    mutable std::size_t m_ShapeBytes = 0;
    mutable std::size_t m_Shapes = 0;
    mutable std::size_t m_CacheHits = 0;

    void ClearShapes()
    {
        m_Cache.clear();
        m_Recency.clear();
        m_ShapeBytes = 0;
    }

    TextLayout Shape(std::string_view text, const FontSelection& selection, bool editable) const
    {
        selection.Validate();
        const ShapeLookup lookup{text, selection, editable};
        if (auto entry = m_Cache.find(lookup); entry != m_Cache.end())
        {
            ++m_CacheHits;
            m_Recency.splice(m_Recency.begin(), m_Recency, entry->second.m_Recency);
            return entry->second.m_Layout;
        }
        auto result =
            Detail::ShapeControlText(text, selection, editable, Detail::ResolveFonts(m_Faces, selection)).front();
        result.m_Service = m_Faces.front()->m_Id;
        result.m_Revision = m_Revision;
        ++m_Shapes;
        const auto bytes = result.StorageBytes() + text.size() * 2 + 2 * sizeof(ShapeKey) + sizeof(Entry) + 256;
        const auto& limits = m_Budget->m_Limits;
        if (bytes > limits.m_MaxShapeBytes || !limits.m_MaxShapeEntries)
            return result;
        while (!m_Cache.empty() &&
               (m_Cache.size() >= limits.m_MaxShapeEntries || m_ShapeBytes + bytes > limits.m_MaxShapeBytes))
        {
            const auto& oldest = m_Recency.back();
            m_ShapeBytes -= m_Cache.at(oldest).m_Bytes;
            m_Cache.erase(oldest);
            m_Recency.pop_back();
        }
        m_Recency.push_front({std::string(text), selection, editable});
        try
        {
            m_Cache.emplace(m_Recency.front(), Entry{result, bytes, m_Recency.begin()});
            m_ShapeBytes += bytes;
        }
        catch (...)
        {
            m_Recency.pop_front();
            throw;
        }
        return result;
    }
};

FontFace::FontFace(const std::filesystem::path& path, FontLimits limits) : m_Impl(std::make_unique<Impl>())
{
    if (!limits.m_MaxFaces || !limits.m_MaxFontBytes || !limits.m_MaxGeometryBytes)
        throw std::invalid_argument("Font face, file and outline budgets must be nonzero.");
    m_Impl->m_Budget = std::make_shared<Detail::FontBudget>();
    m_Impl->m_Budget->m_Limits = limits;
    AddFont({path, {}, 400, false, 0});
}

FontFace::~FontFace()
{
    if (!m_Affinity.IsOwnerThread())
        std::terminate();
}

TextLayout FontFace::Shape(std::string_view text, float size, bool editable) const
{
    VerifyAccess();
    FontSelection selection;
    selection.m_Size = size;
    return m_Impl->Shape(text, selection, editable);
}

TextLayout FontFace::Shape(std::string_view text, const Style& style, bool editable) const
{
    return Shape(text, style.m_FontSize, style, editable);
}

TextLayout FontFace::Shape(std::string_view text, float size, const Style& style, bool editable) const
{
    VerifyAccess();
    return m_Impl->Shape(text, Detail::SelectFont(style, size), editable);
}

std::vector<TextLayout> FontFace::Wrap(std::string_view text, const Style& style, float width) const
{
    VerifyAccess();
    const auto selection = Detail::SelectFont(style, style.m_FontSize);
    selection.Validate();
    auto lines =
        Detail::ShapeControlText(text, selection, false, Detail::ResolveFonts(m_Impl->m_Faces, selection), width);
    for (auto& line : lines)
    {
        line.m_Service = GetIdentity();
        line.m_Revision = GetRevision();
    }
    m_Impl->m_Shapes += lines.size();
    return lines;
}

bool FontFace::Matches(const TextLayout& layout, const Style& style) const
{
    VerifyAccess();
    const auto& selection = layout.m_Selection;
    return layout.m_Service == GetIdentity() && layout.m_Revision == GetRevision() &&
           selection.m_Size == style.m_FontSize && selection.m_Weight == style.m_FontWeight &&
           selection.m_Family == style.m_FontFamily && selection.m_Style == style.m_FontStyle &&
           selection.m_Direction == style.m_TextDirection && selection.m_Language == style.m_Language;
}

std::uint64_t FontFace::AddFont(FontSource source)
{
    VerifyMutation();
    auto& self = *m_Impl;
    auto resource = std::make_shared<Detail::FontResource>(std::move(source), self.m_Budget);
    const auto identity = resource->m_Id;
    std::erase_if(self.m_Budget->m_Resources, [](const auto& weak) { return weak.expired(); });
    self.m_Budget->m_Resources.push_back(resource);
    self.m_Faces.push_back(std::move(resource));
    ++self.m_Revision;
    self.ClearShapes();
    NotifyHosts();
    return identity;
}

bool FontFace::RemoveFont(std::uint64_t identity)
{
    VerifyMutation();
    auto& self = *m_Impl;
    if (identity == GetIdentity())
        throw std::invalid_argument("The primary font remains registered for the lifetime of its font service.");
    const auto removed = std::erase_if(self.m_Faces, [&](const auto& resource) { return resource->m_Id == identity; });
    if (!removed)
        return false;
    ++self.m_Revision;
    self.ClearShapes();
    NotifyHosts();
    return true;
}

std::vector<std::string> FontFace::GetFamilies() const
{
    VerifyAccess();
    std::vector<std::string> result;
    for (const auto& face : m_Impl->m_Faces)
        if (std::find(result.begin(), result.end(), face->m_Source.m_Family) == result.end())
            result.push_back(face->m_Source.m_Family);
    return result;
}

std::uint64_t FontFace::GetIdentity() const
{
    VerifyAccess();
    return m_Impl->m_Faces.front()->m_Id;
}

std::uint64_t FontFace::GetRevision() const
{
    VerifyAccess();
    return m_Impl->m_Revision;
}

FontStats FontFace::GetStats() const
{
    VerifyAccess();
    const auto& self = *m_Impl;
    return {self.m_Faces.size(),
            self.m_Budget->m_Faces.load(),
            self.m_Budget->m_FontBytes.load(),
            self.m_Budget->m_GeometryBytes.load(),
            self.m_ShapeBytes,
            self.m_Cache.size(),
            self.m_Shapes,
            self.m_CacheHits};
}

void FontFace::ClearCaches()
{
    VerifyMutation();
    m_Impl->ClearShapes();
    for (const auto& weak : m_Impl->m_Budget->m_Resources)
        if (const auto face = weak.lock())
            face->ClearGeometry();
}

const GlyphGeometry& FontFace::GetGlyph(std::uint32_t id)
{
    VerifyAccess();
    return m_Impl->m_Faces.front()->Glyph(id);
}

void FontFace::AttachHost(UiContext& context)
{
    VerifyAccess();
    m_Impl->m_Hosts.push_back(&context);
}

void FontFace::DetachHost(UiContext& context) noexcept
{
    std::erase(m_Impl->m_Hosts, &context);
}

void FontFace::VerifyMutation() const
{
    VerifyAccess();
    for (const auto* host : m_Impl->m_Hosts)
        host->VerifyStructuralMutation();
}

void FontFace::NotifyHosts()
{
    for (auto* host : m_Impl->m_Hosts)
        host->FontsChanged();
}

void FontFace::VerifyAccess() const
{
    m_Affinity.VerifyAccess("FontFace");
}
} // namespace EStudioUI
