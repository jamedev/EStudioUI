#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
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
    Circle
};

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

    //! Returns normalized finite bounds, rejecting invalid or overflowing geometry.
    CanvasBounds Bounds() const;

    //! Compares geometry exactly.
    bool operator==(const CanvasGeometry&) const = default;
};

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

//! Returns distance to the segment, rectangle boundary, or circle circumference.
double DistanceToItem(const CanvasGeometry& geometry, CanvasPoint point);

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

    //! Returns the number of live items.
    std::size_t Size() const;

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
