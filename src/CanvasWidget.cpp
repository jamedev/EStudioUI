#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <list>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace EStudioUI
{
namespace
{
constexpr std::string_view DefaultItemCss =
    "* { color: #76c9ff; background: transparent; border: 1.5px solid #76c9ff; } "
    "*:selected { color: #ffce67; border-color: #ffce67; } ";

bool Finite(CanvasPoint point)
{
    return std::isfinite(point.m_X) && std::isfinite(point.m_Y);
}

bool Finite(CanvasBounds bounds)
{
    return std::isfinite(bounds.m_X) && std::isfinite(bounds.m_Y) && std::isfinite(bounds.m_Width) &&
           std::isfinite(bounds.m_Height) && bounds.m_Width >= 0 && bounds.m_Height >= 0 &&
           std::isfinite(bounds.m_X + bounds.m_Width) && std::isfinite(bounds.m_Y + bounds.m_Height);
}

bool Same(CanvasBounds first, CanvasBounds second)
{
    return first.m_X == second.m_X && first.m_Y == second.m_Y && first.m_Width == second.m_Width &&
           first.m_Height == second.m_Height;
}

// The compensated determinant avoids catastrophic cancellation when a long line
// crosses a tiny viewport. Parameter interpolation of huge endpoints loses even
// the viewport boundary itself at high zoom, so pin the dominant coordinate to
// the exact clipping boundary instead.
double DifferenceOfProducts(double a, double b, double c, double d)
{
    const double product = c * d;
    const double correction = std::fma(-c, d, product);
    return std::fma(a, b, -product) + correction;
}

bool ClipLine(CanvasPoint& from, CanvasPoint& to, CanvasBounds bounds)
{
    const double dx = to.m_X - from.m_X;
    const double dy = to.m_Y - from.m_Y;
    if (dx == 0 && dy == 0)
        return bounds.Contains(from);
    const bool horizontal = std::abs(dx) >= std::abs(dy);
    const double first = horizontal ? from.m_X : from.m_Y;
    const double last = horizontal ? to.m_X : to.m_Y;
    const double slope = horizontal ? dy / dx : dx / dy;
    const double intercept = horizontal ? DifferenceOfProducts(from.m_Y, to.m_X, from.m_X, to.m_Y) / dx
                                        : DifferenceOfProducts(from.m_X, to.m_Y, from.m_Y, to.m_X) / dy;
    if (!std::isfinite(slope) || !std::isfinite(intercept))
        return false;
    const double majorMinimum = horizontal ? bounds.m_X : bounds.m_Y;
    const double majorMaximum = majorMinimum + (horizontal ? bounds.m_Width : bounds.m_Height);
    const double minorMinimum = horizontal ? bounds.m_Y : bounds.m_X;
    const double minorMaximum = minorMinimum + (horizontal ? bounds.m_Height : bounds.m_Width);
    double start = std::max(std::min(first, last), majorMinimum);
    double end = std::min(std::max(first, last), majorMaximum);
    if (slope == 0)
    {
        if (intercept < minorMinimum || intercept > minorMaximum)
            return false;
    }
    else
    {
        const double low = (minorMinimum - intercept) / slope;
        const double high = (minorMaximum - intercept) / slope;
        start = std::max(start, std::min(low, high));
        end = std::min(end, std::max(low, high));
    }
    if (start > end)
        return false;
    from = horizontal ? CanvasPoint{start, std::fma(start, slope, intercept)}
                      : CanvasPoint{std::fma(start, slope, intercept), start};
    to = horizontal ? CanvasPoint{end, std::fma(end, slope, intercept)}
                    : CanvasPoint{std::fma(end, slope, intercept), end};
    return Finite(from) && Finite(to);
}

Color Alpha(Color color, float opacity)
{
    color.m_A *= opacity;
    return color;
}
} // namespace

struct CanvasWidget::PaintCache
{
    static constexpr std::size_t s_MaxEntries = 2048;
    static constexpr std::size_t s_MaxBytes = 8 * 1024 * 1024;
    static constexpr std::size_t s_MaxIdentities = 65536;

    struct CurveKey
    {
        const CanvasGeometryData* m_Data = nullptr;
        CanvasPrimitive m_Kind = CanvasPrimitive::Arc;
        double m_Radius = 0;
        int m_ZoomBucket = 0;

        bool operator==(const CurveKey&) const = default;
    };

    struct CurveHash
    {
        std::size_t operator()(const CurveKey& key) const
        {
            return std::hash<const void*>{}(key.m_Data) ^ (std::hash<double>{}(key.m_Radius) << 1) ^
                   (std::hash<int>{}(key.m_ZoomBucket) << 2) ^ static_cast<std::size_t>(key.m_Kind);
        }
    };

    struct TextKey
    {
        const CanvasGeometryData* m_Data = nullptr;
        const FontFace* m_Font = nullptr;

        bool operator==(const TextKey&) const = default;
    };

    struct TextHash
    {
        std::size_t operator()(const TextKey& key) const
        {
            return std::hash<const void*>{}(key.m_Data) ^ (std::hash<const void*>{}(key.m_Font) << 1);
        }
    };

    struct CurveEntry
    {
        std::shared_ptr<const CanvasGeometryData> m_Data;
        std::vector<CanvasPoint> m_Points;
        std::size_t m_Bytes = 0;
        std::list<CurveKey>::iterator m_Recency;
    };

    struct TextEntry
    {
        std::shared_ptr<const CanvasGeometryData> m_Data;
        TextLayout m_Layout;
        std::size_t m_Bytes = 0;
        std::list<TextKey>::iterator m_Recency;
    };

    // Equality compares the complete original child path; hashes never become paint IDs.
    // The monotonically allocated value is not reused after an LRU eviction.
    using IdentityKey = std::pair<std::uint64_t, std::vector<std::size_t>>;

    struct IdentityEntry
    {
        std::uint64_t m_Value = 0;
        std::list<IdentityKey>::iterator m_Recency;
    };

    CanvasCacheStats m_Stats;
    std::size_t m_CurveBytes = 0;
    std::size_t m_TextBytes = 0;
    std::unordered_map<CurveKey, CurveEntry, CurveHash> m_Curves;
    std::list<CurveKey> m_CurveRecency;
    std::unordered_map<TextKey, TextEntry, TextHash> m_Text;
    std::list<TextKey> m_TextRecency;
    std::map<IdentityKey, IdentityEntry> m_Identities;
    std::list<IdentityKey> m_IdentityRecency;
    std::uint64_t m_NextIdentity = 1;
    std::vector<CanvasPoint> m_CurveScratch;
    TextLayout m_TextScratch;

    const std::vector<CanvasPoint>& Curve(const CanvasGeometry& geometry, double zoom)
    {
        // Round upwards so the cached error is never larger than 0.35 logical pixels.
        // A quarter-octave bucket avoids tessellating every wheel/animation frame.
        const int bucket = static_cast<int>(std::ceil(std::log2(zoom) * 4));
        const CurveKey key{geometry.m_Data.get(), geometry.m_Kind, geometry.m_Radius,
                           geometry.m_Kind == CanvasPrimitive::Path ? 0 : bucket};
        if (auto found = m_Curves.find(key); found != m_Curves.end())
        {
            ++m_Stats.m_CurveHits;
            m_CurveRecency.splice(m_CurveRecency.begin(), m_CurveRecency, found->second.m_Recency);
            return found->second.m_Points;
        }
        ++m_Stats.m_CurveMisses;
        auto local = geometry;
        local.m_A = {};
        local.m_B = {};
        if (!FlattenGeometry(local, 0.35 / std::exp2(static_cast<double>(bucket) / 4), m_CurveScratch))
            ++m_Stats.m_LimitedCurves;
        const auto bytes = GeometryBytes(local) + m_CurveScratch.capacity() * sizeof(CanvasPoint) + sizeof(CurveEntry) +
                           sizeof(CurveKey) * 2 + 128;
        if (bytes > s_MaxBytes)
            return m_CurveScratch;
        while (!m_Curves.empty() && (m_Curves.size() >= s_MaxEntries || m_CurveBytes + bytes > s_MaxBytes))
        {
            const auto oldest = m_CurveRecency.back();
            m_CurveBytes -= m_Curves.at(oldest).m_Bytes;
            m_Curves.erase(oldest);
            m_CurveRecency.pop_back();
        }
        m_CurveRecency.push_front(key);
        auto [entry, inserted] = m_Curves.emplace(
            key, CurveEntry{geometry.m_Data, std::move(m_CurveScratch), bytes, m_CurveRecency.begin()});
        (void)inserted;
        m_CurveBytes += bytes;
        return entry->second.m_Points;
    }

    const TextLayout& Text(const CanvasGeometry& geometry, FontFace& font)
    {
        const TextKey key{geometry.m_Data.get(), &font};
        if (auto found = m_Text.find(key); found != m_Text.end())
        {
            ++m_Stats.m_TextHits;
            m_TextRecency.splice(m_TextRecency.begin(), m_TextRecency, found->second.m_Recency);
            return found->second.m_Layout;
        }
        ++m_Stats.m_TextMisses;
        // Shape at a fixed size, then scale the resolution-independent glyph outlines on the GPU.
        m_TextScratch = font.Shape(geometry.m_Data->m_Text, 16);
        const auto bytes = GeometryBytes(geometry) + m_TextScratch.m_Glyphs.capacity() * sizeof(PositionedGlyph) +
                           m_TextScratch.m_Offsets.capacity() * sizeof(std::size_t) +
                           m_TextScratch.m_Carets.capacity() * sizeof(float) + sizeof(TextEntry) + sizeof(TextKey) * 2 +
                           128;
        if (bytes > s_MaxBytes)
            return m_TextScratch;
        while (!m_Text.empty() && (m_Text.size() >= s_MaxEntries || m_TextBytes + bytes > s_MaxBytes))
        {
            const auto oldest = m_TextRecency.back();
            m_TextBytes -= m_Text.at(oldest).m_Bytes;
            m_Text.erase(oldest);
            m_TextRecency.pop_back();
        }
        m_TextRecency.push_front(key);
        auto [entry, inserted] =
            m_Text.emplace(key, TextEntry{geometry.m_Data, std::move(m_TextScratch), bytes, m_TextRecency.begin()});
        (void)inserted;
        m_TextBytes += bytes;
        return entry->second.m_Layout;
    }

    std::uint64_t Identity(std::uint64_t root, const std::vector<std::size_t>& path)
    {
        IdentityKey key{root, path};
        if (const auto found = m_Identities.find(key); found != m_Identities.end())
        {
            m_IdentityRecency.splice(m_IdentityRecency.begin(), m_IdentityRecency, found->second.m_Recency);
            return found->second.m_Value;
        }
        if (m_Identities.size() >= s_MaxIdentities)
        {
            m_Identities.erase(m_IdentityRecency.back());
            m_IdentityRecency.pop_back();
        }
        m_IdentityRecency.push_front(key);
        const auto identity = m_NextIdentity++;
        m_Identities.emplace(std::move(key), IdentityEntry{identity, m_IdentityRecency.begin()});
        return identity;
    }
};

CanvasWidget::CanvasWidget()
    : Widget("canvas"), m_Scene(std::make_shared<CanvasScene>()), m_PaintCache(std::make_unique<PaintCache>())
{
    SetWidth(SizeRule::Fill());
    SetHeight(SizeRule::Fill());
    SetStyleProperty(StyleProperty::ClipContent, true);
    std::string error;
    m_ItemStyleSheet.Parse(DefaultItemCss, error);
    RegisterItemStyle("canvas-item");
}

CanvasWidget::~CanvasWidget()
{
    if (!IsOwnerThread())
        std::terminate();
}

void CanvasWidget::SetScene(std::shared_ptr<CanvasScene> scene)
{
    VerifyStructuralMutation();
    m_Scene = scene ? std::move(scene) : std::make_shared<CanvasScene>();
    m_Preview.clear();
    m_SelectionRectangle.reset();
    m_VisibleItems.clear();
    SceneChanged();
}

CanvasScene& CanvasWidget::GetScene()
{
    VerifyAccess();
    return *m_Scene;
}

const CanvasScene& CanvasWidget::GetScene() const
{
    VerifyAccess();
    return *m_Scene;
}

void CanvasWidget::SceneChanged()
{
    VerifyStructuralMutation();
    m_CachedRevision = std::numeric_limits<std::uint64_t>::max();
    RequestRepaint();
}

CanvasPoint CanvasWidget::ToWorld(Point point) const
{
    VerifyAccess();
    const auto content = GetContentBounds();
    return {m_Center.m_X + (static_cast<double>(point.m_X) - content.m_X - content.m_Width * 0.5) / m_Zoom,
            m_Center.m_Y + (static_cast<double>(point.m_Y) - content.m_Y - content.m_Height * 0.5) / m_Zoom};
}

Point CanvasWidget::ToScreen(CanvasPoint point) const
{
    VerifyAccess();
    const auto content = GetContentBounds();
    return {static_cast<float>((point.m_X - m_Center.m_X) * m_Zoom + content.m_X + content.m_Width * 0.5),
            static_cast<float>((point.m_Y - m_Center.m_Y) * m_Zoom + content.m_Y + content.m_Height * 0.5)};
}

CanvasPoint CanvasWidget::GetCenter() const
{
    VerifyAccess();
    return m_Center;
}

double CanvasWidget::GetZoom() const
{
    VerifyAccess();
    return m_Zoom;
}

void CanvasWidget::SetCamera(CanvasPoint center, double zoom)
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    if (!Finite(center) || !std::isfinite(zoom) || zoom <= 0)
        throw std::invalid_argument("Canvas camera values must be finite and zoom must be positive.");
    zoom = std::clamp(zoom, 1e-6, 1e6);
    if (center == m_Center && zoom == m_Zoom)
        return;
    m_Center = center;
    m_Zoom = zoom;
    if (m_Panning && m_LastPointer)
    {
        m_PanStart = *m_LastPointer;
        m_PanCenter = center;
    }
    m_CachedRevision = std::numeric_limits<std::uint64_t>::max();
    PlaceHostedWidgets();
    if (m_Hosts.empty())
        RequestRepaint();
    else
        Invalidate();
    m_ViewChanged.Emit();
}

void CanvasWidget::FitScene(double margin)
{
    VerifyAccess();
    if (!std::isfinite(margin) || margin < 0)
        throw std::invalid_argument("Canvas fit margin must be finite and nonnegative.");
    if (m_Scene->Size() == 0)
    {
        SetCamera({}, 1);
        return;
    }
    const auto bounds = m_Scene->GetBounds();
    const auto content = GetContentBounds();
    const double width = std::max(1.0, content.m_Width - 2 * margin);
    const double height = std::max(1.0, content.m_Height - 2 * margin);
    const double zoom = std::min(width / std::max(bounds.m_Width, 1.0), height / std::max(bounds.m_Height, 1.0));
    SetCamera({bounds.m_X + bounds.m_Width * 0.5, bounds.m_Y + bounds.m_Height * 0.5}, zoom);
}

CanvasBounds CanvasWidget::GetViewportWorldBounds() const
{
    VerifyAccess();
    const auto content = GetContentBounds();
    const auto origin = ToWorld({content.m_X, content.m_Y});
    return {origin.m_X, origin.m_Y, content.m_Width / m_Zoom, content.m_Height / m_Zoom};
}

void CanvasWidget::SetGridVisible(bool visible)
{
    VerifyAccess();
    if (m_GridVisible == visible)
        return;
    m_GridVisible = visible;
    RequestRepaint();
}

void CanvasWidget::SetPanMode(bool enabled)
{
    VerifyAccess();
    m_PanMode = enabled;
}

bool CanvasWidget::IsPanning() const
{
    VerifyAccess();
    return m_Panning;
}

std::uint32_t CanvasWidget::RegisterItemStyle(std::string type, std::vector<std::string> classes, std::string id)
{
    VerifyStructuralMutation();
    if (m_Palette.size() >= std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("Canvas palette is full.");
    PaletteEntry entry;
    entry.m_Node.m_Type = std::move(type);
    entry.m_Node.m_Classes = std::move(classes);
    entry.m_Node.m_Id = std::move(id);
    m_Palette.push_back(std::move(entry));
    CompilePalette();
    SceneChanged();
    return static_cast<std::uint32_t>(m_Palette.size() - 1);
}

bool CanvasWidget::SetItemStyleSheet(std::string_view css, std::string& error)
{
    VerifyStructuralMutation();
    StyleSheet candidate;
    if (!candidate.Parse(std::string(DefaultItemCss) + std::string(css), error))
        return false;
    m_ItemStyleSheet = std::move(candidate);
    CompilePalette();
    SceneChanged();
    return true;
}

void CanvasWidget::CompilePalette()
{
    StyleNode parent;
    parent.m_Type = "canvas";
    m_PaintMargin = 2;
    for (auto& entry : m_Palette)
    {
        auto node = entry.m_Node;
        node.m_Parent = &parent;
        for (unsigned selected = 0; selected < 2; ++selected)
        {
            node.m_State = selected ? static_cast<unsigned>(Selected) : 0U;
            auto& style = entry.m_Styles[selected];
            style = m_ItemStyleSheet.Compute(node);
            // Dense geometry is positioned by the scene, not CSS layout or
            // widget timelines. Paint values remain shared between all items.
            style.m_Transform = {};
            style.m_Transitions.clear();
            style.m_Animations.clear();
            style.m_Displayed = true;
            m_PaintMargin = std::max(m_PaintMargin, static_cast<double>(style.m_BorderWidth) + 2);
            const auto shadowReach = [&](const Shadow& shadow)
            {
                if (!shadow.m_Inset && shadow.m_Color.m_A > 0)
                    m_PaintMargin = std::max(m_PaintMargin,
                                             static_cast<double>(std::max(std::abs(shadow.m_X), std::abs(shadow.m_Y))) +
                                                 std::max(0.0f, shadow.m_Spread) + shadow.m_Blur + 2);
            };
            shadowReach(style.m_Shadow);
            for (const auto& shadow : style.m_ShadowLayers.m_Layers)
                shadowReach(shadow);
        }
    }
}

void CanvasWidget::SetPreview(std::vector<CanvasGeometry> geometry, std::uint32_t style)
{
    VerifyStructuralMutation();
    for (const auto& item : geometry)
        if (!Finite(item.m_A) || !Finite(item.m_B) || !std::isfinite(item.m_Radius) || item.m_Radius < 0 ||
            !Finite(item.Bounds()))
            throw std::invalid_argument("Canvas preview geometry must be finite.");
    m_Preview = std::move(geometry);
    m_PreviewStyle = style;
    RequestRepaint();
}

void CanvasWidget::SetSelectionRectangle(std::optional<CanvasBounds> bounds)
{
    VerifyAccess();
    if (bounds && !Finite(*bounds))
        throw std::invalid_argument("Canvas selection bounds must be finite and nonnegative.");
    m_SelectionRectangle = bounds;
    RequestRepaint();
}

Widget& CanvasWidget::HostWidget(std::unique_ptr<Widget> widget, CanvasBounds bounds)
{
    VerifyStructuralMutation();
    if (!widget || !Finite(bounds))
        throw std::invalid_argument("Canvas hosts require a widget and finite, nonnegative bounds.");
    auto& child = AddChild(std::move(widget));
    m_Hosts.push_back({WidgetRef(&child), bounds});
    PlaceHostedWidgets();
    return child;
}

bool CanvasWidget::RemoveHostedWidget(Widget& widget)
{
    VerifyStructuralMutation();
    const auto previous = m_Hosts.size();
    std::erase_if(m_Hosts, [&](const HostedWidget& host) { return host.m_Widget.Get() == &widget; });
    return m_Hosts.size() != previous && RemoveChild(widget);
}

void CanvasWidget::PlaceHostedWidgets()
{
    std::erase_if(m_Hosts, [](const HostedWidget& host) { return host.m_Widget.Get() == nullptr; });
    for (const auto& host : m_Hosts)
    {
        auto* child = host.m_Widget.Get();
        const auto position = ToScreen({host.m_Bounds.m_X, host.m_Bounds.m_Y});
        const double width = host.m_Bounds.m_Width * m_Zoom;
        const double height = host.m_Bounds.m_Height * m_Zoom;
        if (std::isfinite(position.m_X) && std::isfinite(position.m_Y) && std::isfinite(width) &&
            std::isfinite(height) && width <= std::numeric_limits<float>::max() &&
            height <= std::numeric_limits<float>::max())
            child->SetFloatingBounds(
                {position.m_X, position.m_Y, static_cast<float>(width), static_cast<float>(height)});
        else
            child->SetFloatingBounds({-1, -1, 0, 0});
    }
}

void CanvasWidget::UpdateVisibleItems() const
{
    const auto clip = GetContentBounds().Intersect(GetClip());
    if (clip.m_Width <= 0 || clip.m_Height <= 0)
    {
        m_VisibleItems.clear();
        m_QueryVisits = 0;
        m_CachedRevision = std::numeric_limits<std::uint64_t>::max();
        return;
    }
    const auto origin = ToWorld({clip.m_X, clip.m_Y});
    const double margin = m_PaintMargin / m_Zoom;
    const CanvasBounds query{origin.m_X - margin, origin.m_Y - margin, clip.m_Width / m_Zoom + 2 * margin,
                             clip.m_Height / m_Zoom + 2 * margin};
    if (m_CachedRevision == m_Scene->GetRevision() && Same(query, m_CachedQuery))
        return;
    m_Scene->Query(query, m_VisibleItems);
    m_QueryVisits = m_Scene->GetLastQueryVisits();
    std::sort(m_VisibleItems.begin(), m_VisibleItems.end(), [&](CanvasItemId first, CanvasItemId second)
              { return m_Scene->Get(first)->m_Order < m_Scene->Get(second)->m_Order; });
    m_CachedQuery = query;
    m_CachedRevision = m_Scene->GetRevision();
}

std::size_t CanvasWidget::GetVisibleItemCount() const
{
    VerifyAccess();
    UpdateVisibleItems();
    return m_VisibleItems.size();
}

std::size_t CanvasWidget::GetQueryVisitCount() const
{
    VerifyAccess();
    UpdateVisibleItems();
    return m_QueryVisits;
}

CanvasCacheStats CanvasWidget::GetCacheStats() const
{
    VerifyAccess();
    auto stats = m_PaintCache->m_Stats;
    stats.m_CurveEntries = m_PaintCache->m_Curves.size();
    stats.m_TextEntries = m_PaintCache->m_Text.size();
    stats.m_Bytes = m_PaintCache->m_CurveBytes + m_PaintCache->m_TextBytes;
    return stats;
}

Signal<>& CanvasWidget::ViewChanged()
{
    VerifyAccess();
    return m_ViewChanged;
}

bool CanvasWidget::IsFocusable() const
{
    VerifyAccess();
    return true;
}

void CanvasWidget::DrawSegment(Renderer& renderer, CanvasPoint from, CanvasPoint to, const Style& style, Rect clip,
                               float opacity, CanvasPoint origin) const
{
    const auto content = GetContentBounds();
    // Subtract the camera before adding local curve coordinates, preserving small
    // features when both the curve origin and the camera are far from world zero.
    const CanvasPoint relativeOrigin{origin.m_X - m_Center.m_X, origin.m_Y - m_Center.m_Y};
    from = {relativeOrigin.m_X + from.m_X, relativeOrigin.m_Y + from.m_Y};
    to = {relativeOrigin.m_X + to.m_X, relativeOrigin.m_Y + to.m_Y};
    const double centerX = content.m_X + content.m_Width * 0.5;
    const double centerY = content.m_Y + content.m_Height * 0.5;
    const double reach = style.m_BorderWidth * 0.5 + 2;
    if (ClipLine(from, to,
                 {(clip.m_X - reach - centerX) / m_Zoom, (clip.m_Y - reach - centerY) / m_Zoom,
                  (clip.m_Width + 2 * reach) / m_Zoom, (clip.m_Height + 2 * reach) / m_Zoom}))
        renderer.DrawLine(
            {static_cast<float>(from.m_X * m_Zoom + centerX), static_cast<float>(from.m_Y * m_Zoom + centerY)},
            {static_cast<float>(to.m_X * m_Zoom + centerX), static_cast<float>(to.m_Y * m_Zoom + centerY)},
            Alpha(style.m_Color, opacity), style.m_BorderWidth, clip);
}

void CanvasWidget::DrawGeometry(Renderer& renderer, FontFace& font, const CanvasGeometry& geometry, const Style& style,
                                Rect clip, float opacity, bool selected, std::uint64_t rootIdentity,
                                std::vector<std::size_t>& childPath) const
{
    if (geometry.m_Kind == CanvasPrimitive::Group)
    {
        if (!geometry.m_Data)
            return;
        std::vector<std::size_t> visibleChildren;
        QueryCanvasGroup(geometry, m_CachedQuery, visibleChildren);
        m_PaintCache->m_Stats.m_GroupCandidates += visibleChildren.size();
        for (const auto index : visibleChildren)
        {
            const auto& data = *geometry.m_Data;
            const auto child = TranslateGeometry(data.m_Children[index], geometry.m_A);
            const Style* childStyle = &style;
            if (!data.m_ChildStyles.empty())
            {
                const auto palette = data.m_ChildStyles[index];
                childStyle = &m_Palette[palette < m_Palette.size() ? palette : 0].m_Styles[selected ? 1 : 0];
            }
            childPath.push_back(index);
            DrawGeometry(renderer, font, child, *childStyle, clip, opacity, selected, rootIdentity, childPath);
            childPath.pop_back();
        }
        return;
    }
    // Groups have no paint layer: their style is only a fallback for children.
    // Apply a complete leaf style once, regardless of the number of enclosing groups.
    if (!style.m_Displayed || style.m_Opacity <= 0)
        return;
    if (!childPath.empty())
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(m_PaintCache.get()),
                                  m_PaintCache->Identity(rootIdentity, childPath));
    opacity *= style.m_Opacity;
    if (geometry.m_Kind == CanvasPrimitive::Line)
    {
        DrawSegment(renderer, geometry.m_A, geometry.m_B, style, clip, opacity);
        return;
    }
    if (geometry.m_Kind == CanvasPrimitive::Arc || geometry.m_Kind == CanvasPrimitive::Spline ||
        geometry.m_Kind == CanvasPrimitive::Path)
    {
        if (!geometry.m_Data)
            return;
        const auto& points = m_PaintCache->Curve(geometry, m_Zoom);
        for (std::size_t index = 1; index < points.size(); ++index)
            DrawSegment(renderer, points[index - 1], points[index], style, clip, opacity, geometry.m_A);
        return;
    }
    if (geometry.m_Kind == CanvasPrimitive::Circle)
    {
        const auto center = ToScreen(geometry.m_A);
        const double radius = geometry.m_Radius * m_Zoom;
        if (std::isfinite(center.m_X) && std::isfinite(center.m_Y) && std::isfinite(radius) &&
            radius <= std::numeric_limits<float>::max() * 0.25)
            renderer.DrawCircle(center, static_cast<float>(radius), style, clip, opacity);
        return;
    }
    const auto bounds = geometry.Bounds();
    const auto position = ToScreen({bounds.m_X, bounds.m_Y});
    const double width = bounds.m_Width * m_Zoom;
    const double height = bounds.m_Height * m_Zoom;
    if (!std::isfinite(position.m_X) || !std::isfinite(position.m_Y) || !std::isfinite(width) ||
        !std::isfinite(height) || width > std::numeric_limits<float>::max() * 0.25 ||
        height > std::numeric_limits<float>::max() * 0.25)
        return;
    const Rect screenBounds{position.m_X, position.m_Y, static_cast<float>(width), static_cast<float>(height)};
    if (geometry.m_Kind == CanvasPrimitive::Text)
    {
        if (!geometry.m_Data || geometry.m_Data->m_Text.empty() || width <= 0 || height <= 0)
            return;
        const double scale = geometry.m_Data->m_FontSize * m_Zoom / 16;
        if (!std::isfinite(scale) || scale <= 0 || scale > std::numeric_limits<float>::max() * 0.25 ||
            width / scale > std::numeric_limits<float>::max() * 0.25 ||
            height / scale > std::numeric_limits<float>::max() * 0.25)
            return;
        const auto& layout = m_PaintCache->Text(geometry, font);
        renderer.PushState();
        renderer.PushClip(screenBounds.Intersect(clip));
        renderer.PushState({static_cast<float>(scale), 0, 0, static_cast<float>(scale), position.m_X, position.m_Y});
        renderer.DrawText(layout, {0, layout.m_Ascender}, Alpha(style.m_Color, opacity),
                          {0, 0, static_cast<float>(width / scale), static_cast<float>(height / scale)});
        renderer.PopState();
        renderer.PopState();
        return;
    }
    renderer.DrawBox(screenBounds, style, clip, opacity);
}

void CanvasWidget::DrawGrid(Renderer& renderer, Rect clip, float opacity) const
{
    const auto world = GetViewportWorldBounds();
    const double raw = 32 / m_Zoom;
    const double base = std::pow(10.0, std::floor(std::log10(raw)));
    const double fraction = raw / base;
    const double spacing = base * (fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10);
    const auto color = Alpha(GetStyle().m_GridColor, opacity);
    const auto firstGrid = [spacing](double coordinate)
    {
        const double first = coordinate - std::fmod(coordinate, spacing);
        return first < coordinate ? first + spacing : first;
    };
    const double firstX = firstGrid(world.m_X);
    const double firstY = firstGrid(world.m_Y);
    const auto verticalCount = world.m_X + spacing == world.m_X
                                   ? std::size_t{0}
                                   : static_cast<std::size_t>(std::min(4096.0, std::ceil(world.m_Width / spacing) + 1));
    const auto horizontalCount =
        world.m_Y + spacing == world.m_Y
            ? std::size_t{0}
            : static_cast<std::size_t>(std::min(4096.0, std::ceil(world.m_Height / spacing) + 1));
    for (std::size_t i = 0; i < verticalCount; ++i)
    {
        const float x = ToScreen({firstX + static_cast<double>(i) * spacing, m_Center.m_Y}).m_X;
        renderer.DrawLine({x, clip.m_Y}, {x, clip.m_Y + clip.m_Height}, color, 1, clip);
    }
    for (std::size_t i = 0; i < horizontalCount; ++i)
    {
        const float y = ToScreen({m_Center.m_X, firstY + static_cast<double>(i) * spacing}).m_Y;
        renderer.DrawLine({clip.m_X, y}, {clip.m_X + clip.m_Width, y}, color, 1, clip);
    }
    const auto origin = ToScreen({});
    const auto axisColor = Alpha(GetStyle().m_Color, opacity * 0.25f);
    if (origin.m_X >= clip.m_X && origin.m_X <= clip.m_X + clip.m_Width)
        renderer.DrawLine({origin.m_X, clip.m_Y}, {origin.m_X, clip.m_Y + clip.m_Height}, axisColor, 1.5f, clip);
    if (origin.m_Y >= clip.m_Y && origin.m_Y <= clip.m_Y + clip.m_Height)
        renderer.DrawLine({clip.m_X, origin.m_Y}, {clip.m_X + clip.m_Width, origin.m_Y}, axisColor, 1.5f, clip);
}

void CanvasWidget::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    const auto clip = GetContentBounds().Intersect(GetClip());
    if (clip.m_Width <= 0 || clip.m_Height <= 0)
        return;
    renderer.PushState();
    renderer.PushClip(clip);
    if (m_GridVisible)
        DrawGrid(renderer, clip, opacity);
    UpdateVisibleItems();
    m_PaintCache->m_Stats.m_GroupCandidates = 0;
    std::vector<std::size_t> childPath;
    renderer.PushState();
    for (const auto id : m_VisibleItems)
    {
        const auto* item = m_Scene->Get(id);
        if (!item)
            continue;
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(this),
                                  (static_cast<std::uint64_t>(id.m_Generation) << 32) | id.m_Index);
        const auto& palette = m_Palette[item->m_Style < m_Palette.size() ? item->m_Style : 0];
        DrawGeometry(renderer, font, item->m_Geometry, palette.m_Styles[item->m_Selected ? 1 : 0], clip, opacity,
                     item->m_Selected, (static_cast<std::uint64_t>(id.m_Generation) << 32) | id.m_Index, childPath);
    }
    renderer.PopState();
    const auto& previewStyle = m_Palette[m_PreviewStyle < m_Palette.size() ? m_PreviewStyle : 0].m_Styles[1];
    renderer.PushState();
    for (std::size_t index = 0; index < m_Preview.size(); ++index)
    {
        const auto& geometry = m_Preview[index];
        if (!geometry.Bounds().Intersects(m_CachedQuery))
            continue;
        // Preview roots occupy the generation-zero namespace, disjoint from all live scene IDs.
        renderer.SetPaintIdentity(reinterpret_cast<std::uintptr_t>(&m_Preview), index);
        DrawGeometry(renderer, font, geometry, previewStyle, clip, opacity * 0.7f, true, index, childPath);
    }
    renderer.PopState();
    if (m_SelectionRectangle)
    {
        Style style;
        style.m_Background = {0.3f, 0.65f, 1, 0.12f};
        style.m_BorderColor = {0.3f, 0.65f, 1, 0.8f};
        style.m_BorderWidth = 1;
        const auto bounds = *m_SelectionRectangle;
        const auto position = ToScreen({bounds.m_X, bounds.m_Y});
        renderer.DrawBox({position.m_X, position.m_Y, static_cast<float>(bounds.m_Width * m_Zoom),
                          static_cast<float>(bounds.m_Height * m_Zoom)},
                         style, clip, opacity);
    }
    renderer.PopState();
}

void CanvasWidget::OnPointerDown(const PointerEvent& event)
{
    m_LastPointer = event.m_Position;
    m_Panning = m_PanMode || m_SpaceDown || event.m_Modifiers.m_Alt;
    if (m_Panning)
    {
        m_PanStart = event.m_Position;
        m_PanCenter = m_Center;
    }
}

void CanvasWidget::OnPointerMove(const PointerEvent& event)
{
    m_LastPointer = event.m_Position;
    if (m_Panning)
        SetCamera({m_PanCenter.m_X - (event.m_Position.m_X - m_PanStart.m_X) / m_Zoom,
                   m_PanCenter.m_Y - (event.m_Position.m_Y - m_PanStart.m_Y) / m_Zoom},
                  m_Zoom);
}

void CanvasWidget::OnPointerUp(const PointerEvent& event)
{
    m_LastPointer = event.m_Position;
    m_Panning = false;
}

void CanvasWidget::OnCaptureLost()
{
    m_Panning = false;
}

void CanvasWidget::OnKey(Key key, bool down, bool, Modifiers)
{
    if (key == Key::Space)
        m_SpaceDown = down;
}

void CanvasWidget::OnFocus(bool focused)
{
    if (!focused)
    {
        m_SpaceDown = false;
        m_Panning = false;
    }
}

void CanvasWidget::OnLayout()
{
    PlaceHostedWidgets();
}

bool CanvasWidget::OnScroll(Point delta, Modifiers)
{
    if (!std::isfinite(delta.m_Y) || delta.m_Y == 0)
        return false;
    const auto content = GetContentBounds();
    Point anchor{content.m_X + content.m_Width * 0.5f, content.m_Y + content.m_Height * 0.5f};
    if (const auto* context = GetContext(); context && context->GetPointerPosition())
    {
        const auto point = context->ToLayoutPoint(*this, *context->GetPointerPosition());
        if (content.Contains(point))
            anchor = point;
    }
    else if (m_LastPointer && content.Contains(*m_LastPointer))
        anchor = *m_LastPointer;
    m_LastPointer = anchor;
    const auto before = ToWorld(anchor);
    const double zoom =
        std::clamp(m_Zoom * std::exp(std::clamp(static_cast<double>(delta.m_Y) * 0.15, -20.0, 20.0)), 1e-6, 1e6);
    const double dx = static_cast<double>(anchor.m_X) - content.m_X - content.m_Width * 0.5;
    const double dy = static_cast<double>(anchor.m_Y) - content.m_Y - content.m_Height * 0.5;
    SetCamera({before.m_X - dx / zoom, before.m_Y - dy / zoom}, zoom);
    return true;
}
} // namespace EStudioUI
