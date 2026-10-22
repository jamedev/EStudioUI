#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace EStudioUI
{
//! A double-precision position or displacement in canvas world units.
struct CanvasPoint
{
    //! Horizontal world coordinate.
    double m_X = 0;

    //! Vertical world coordinate; positive values point downward.
    double m_Y = 0;

    //! Compares world coordinates exactly.
    bool operator==(const CanvasPoint&) const = default;
};

//! An axis-aligned world rectangle, including its boundary and zero-size axes.
struct CanvasBounds
{
    //! Minimum horizontal coordinate.
    double m_X = 0;

    //! Minimum vertical coordinate.
    double m_Y = 0;

    //! Nonnegative horizontal extent.
    double m_Width = 0;

    //! Nonnegative vertical extent.
    double m_Height = 0;

    //! Tests containment including all four edges.
    bool Contains(CanvasPoint point) const;

    //! Tests overlap including touching and degenerate bounds.
    bool Intersects(const CanvasBounds& other) const;

    //! Compares all bounds components exactly.
    bool operator==(const CanvasBounds&) const = default;
};

//! Lightweight geometric primitives stored without individual widget objects.
enum class CanvasPrimitive
{
    Line,
    Rectangle,
    Circle,
    Arc,
    Spline,
    Path,
    Text,
    Group
};

struct CanvasGeometryData;
struct CanvasGroupIndex;

//! Analytic geometry; circles use A as their center and Radius as their radius.
struct CanvasGeometry
{
    //! Geometric interpretation of the coordinates.
    CanvasPrimitive m_Kind = CanvasPrimitive::Line;

    //! First endpoint, rectangle corner, or circle center.
    CanvasPoint m_A;

    //! Second endpoint or rectangle corner; ignored for circles.
    CanvasPoint m_B;

    //! Nonnegative circle radius; ignored for lines and rectangles.
    double m_Radius = 0;

    //! Shared immutable data; retaining and mutating an alias after submission is unsupported.
    std::shared_ptr<const CanvasGeometryData> m_Data = nullptr;

    //! Returns finite bounds including indexed edit handles (arc centers and spline controls).
    CanvasBounds Bounds() const;

    //! Compares geometry exactly.
    bool operator==(const CanvasGeometry& other) const;
};

//! Optional payload; use factories and never mutate published data through a retained alias.
struct CanvasGeometryData
{
    //! Path vertices or cubic controls, relative to the owning geometry's A coordinate.
    std::vector<CanvasPoint> m_Points;

    //! UTF-8 annotation content without embedded line breaks.
    std::string m_Text;

    //! Group geometry in coordinates relative to the owning group's A coordinate.
    std::vector<CanvasGeometry> m_Children;

    //! Optional child palette indices; an empty vector inherits the group style.
    std::vector<std::uint32_t> m_ChildStyles;

    //! Factory-built immutable child BVH; excluded from logical equality and persistence.
    std::shared_ptr<const CanvasGroupIndex> m_GroupIndex;

    //! Arc start angle in radians, clockwise in the y-down world.
    double m_StartAngle = 0;

    //! Signed arc sweep in radians, with magnitude no greater than one full turn.
    double m_SweepAngle = 0;

    //! Text size in world units, rendered inside the explicit annotation rectangle.
    double m_FontSize = 18;

    //! Whether a polyline joins its final vertex back to its first.
    bool m_Closed = false;

    //! Compares immutable payload contents, including nested geometry.
    bool operator==(const CanvasGeometryData& other) const;
};

//! Creates a circular arc with a signed sweep of at most one full turn.
CanvasGeometry MakeCanvasArc(CanvasPoint center, double radius, double startAngle, double sweepAngle);

//! Creates a connected cubic Bezier chain from 3n+1 local control points.
CanvasGeometry MakeCanvasSpline(CanvasPoint origin, std::vector<CanvasPoint> controls);

//! Creates an open or closed stroked polyline from at least two local vertices.
CanvasGeometry MakeCanvasPath(CanvasPoint origin, std::vector<CanvasPoint> points, bool closed = false);

//! Creates UTF-8 text clipped to an explicit world rectangle.
CanvasGeometry MakeCanvasText(CanvasBounds bounds, std::string text, double fontSize = 18);

//! Creates an ordered group and immutable child BVH, with local geometry and optional child styles.
CanvasGeometry MakeCanvasGroup(CanvasPoint origin, std::vector<CanvasGeometry> children,
                               std::vector<std::uint32_t> styles = {});

//! Flattens line/rectangle/circle/arc/spline/path to world vertices; false reports a bounded quality shortfall.
bool FlattenGeometry(const CanvasGeometry& geometry, double tolerance, std::vector<CanvasPoint>& output,
                     std::size_t maxPoints = 65536);

//! Replaces output with geometric endpoints, centers, corners, and curve edit handles.
void GeometrySnapPoints(const CanvasGeometry& geometry, std::vector<CanvasPoint>& output);

//! Replaces output with snap points inside worldBounds, using nested group BVHs.
void GeometrySnapPoints(const CanvasGeometry& geometry, const CanvasBounds& worldBounds,
                        std::vector<CanvasPoint>& output);

//! Replaces output with intersecting direct group-child indices in original painter order.
void QueryCanvasGroup(const CanvasGeometry& geometry, const CanvasBounds& worldBounds,
                      std::vector<std::size_t>& output);

//! Estimates retained shared payload bytes, conservatively counting repeated nested references.
std::size_t GeometryBytes(const CanvasGeometry& geometry);

//! A stable scene-local identity; recycled slots receive a new generation.
struct CanvasItemId
{
    //! Slot index; an identity is valid only when its generation is nonzero.
    std::uint32_t m_Index = 0;

    //! Nonzero lifetime generation for the slot.
    std::uint32_t m_Generation = 0;

    //! Tests whether this is a nonempty identity, without consulting a scene.
    explicit operator bool() const;

    //! Compares both the slot and lifetime generation.
    bool operator==(const CanvasItemId&) const = default;
};

//! One densely stored scene item with a shared style palette index.
struct CanvasItem
{
    //! Stable identity retained across movement and unrelated deletion.
    CanvasItemId m_Id;

    //! Analytic world-space geometry.
    CanvasGeometry m_Geometry;

    //! Application-owned shared CSS style palette index.
    std::uint32_t m_Style = 0;

    //! Selection state independent of the geometry and style identity.
    bool m_Selected = false;

    //! Monotonic insertion order used for stable painter ordering.
    std::uint64_t m_Order = 0;
};

//! Returns outline/text-box/nearest-child distance; splines use bounded adaptive tessellation.
double DistanceToItem(const CanvasGeometry& geometry, CanvasPoint point, double curveTolerance = 0.01);

//! Translates geometry in world units, rejecting invalid or overflowing results.
CanvasGeometry TranslateGeometry(const CanvasGeometry& geometry, CanvasPoint delta);

//! Dense scene storage and a balanced dynamic bounding-volume hierarchy.
class CanvasScene
{
  public:
    //! Creates an empty scene without graphics or platform resources.
    CanvasScene();

    //! Releases item storage and the spatial index.
    ~CanvasScene();

    //! Scenes own their item identity namespace.
    CanvasScene(const CanvasScene&) = delete;

    //! Scenes own their item identity namespace.
    CanvasScene& operator=(const CanvasScene&) = delete;

    //! Preallocates item, identity, and BVH storage for a known workload.
    void Reserve(std::size_t count);

    //! Inserts finite geometry, throwing invalid_argument for invalid bounds.
    CanvasItemId Add(const CanvasGeometry& geometry, std::uint32_t style = 0);

    //! Removes a live item; stale or empty identities return false.
    bool Remove(CanvasItemId id);

    //! Updates live geometry and its BVH leaf; stale identities return false.
    bool Update(CanvasItemId id, const CanvasGeometry& geometry);

    //! Changes a live item's shared style index.
    bool SetStyle(CanvasItemId id, std::uint32_t style);

    //! Changes a live item's selected state.
    bool SetSelected(CanvasItemId id, bool selected);

    //! Restores painter order, for example when replaying an undo command.
    bool SetOrder(CanvasItemId id, std::uint64_t order);

    //! Looks up an item; its pointer is invalidated by structural scene mutations.
    const CanvasItem* Get(CanvasItemId id) const;

    //! Resolves a live identity's dense sibling position without scanning items.
    std::optional<std::size_t> GetIndex(CanvasItemId id) const;

    //! Returns the selected-item count without scanning the scene.
    std::size_t GetSelectedCount() const;

    //! Returns a bounded selection page; advancing to offset visits only selected identities.
    std::vector<CanvasItemId> GetSelected(std::size_t offset = 0, std::size_t limit = 256) const;

    //! Clears current selection in work proportional to the selected set.
    void ClearSelection();

    //! Finds the topmost geometry at a point through the BVH without materializing candidates.
    CanvasItemId HitTest(CanvasPoint point, double tolerance) const;

    //! Returns a revision changing only on item insertion, removal or replacement.
    std::uint64_t GetStructureRevision() const;

    //! Returns the number of live items.
    std::size_t Size() const;

    //! Estimates retained scene and selection-index bytes, excluding geometry payloads shared with clients.
    std::size_t GetStorageBytes() const;

    //! Removes all items and invalidates every previously issued identity.
    void Clear();

    //! Atomically consumes source, invalidates old target IDs, and leaves old target storage in source.
    void ReplaceWith(CanvasScene& source);

    //! Replaces output with intersecting identities; order is unspecified and bounds must be finite/nonnegative.
    void Query(const CanvasBounds& bounds, std::vector<CanvasItemId>& output) const;

    //! Returns a monotonically increasing content revision for view invalidation.
    std::uint64_t GetRevision() const;

    //! Returns the number of BVH nodes visited by the most recent query.
    std::size_t GetLastQueryVisits() const;

    //! Returns live world bounds, or an empty origin rectangle for an empty scene.
    CanvasBounds GetBounds() const;

    //! Exposes contiguous read-only live data; references are invalidated by structural mutations.
    const std::vector<CanvasItem>& GetItems() const;

  private:
    //! Implements dense storage, generation slots, and the dynamic AABB tree.
    struct Impl;

    //! Hides spatial-index implementation details from model clients.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
