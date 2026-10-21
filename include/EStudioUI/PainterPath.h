#pragma once

#include "Types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace EStudioUI
{
//! One flattened path contour in local logical coordinates.
struct PainterContour
{
    //! Ordered vertices; closed contours omit a repeated last vertex.
    std::vector<Point> m_Points;

    //! Connects the last vertex back to the first.
    bool m_Closed = false;
};

//! A reusable, bounded path with adaptive quadratic/cubic/arc subdivision.
class PainterPath
{
  public:
    //! Removes all commands and invalidates cached geometry.
    void Clear();

    //! Starts a separate contour at a finite point.
    void MoveTo(Point point);

    //! Appends a straight segment; requires a current contour.
    void LineTo(Point point);

    //! Appends a quadratic Bezier segment using one control point.
    void QuadTo(Point control, Point end);

    //! Appends a cubic Bezier segment using two control points.
    void CubicTo(Point firstControl, Point secondControl, Point end);

    //! Appends a circular arc in radians, connecting to its start when needed.
    void ArcTo(Point center, float radius, float startAngle, float sweepAngle);

    //! Closes and ends the current contour; begin another with MoveTo or ArcTo.
    void Close();

    //! Returns cached contours at positive local tolerance; output is capped at 65536 vertices.
    const std::vector<PainterContour>& Flatten(float tolerance = 0.35f) const;

    //! Reports whether the latest subdivision reached a vertex/depth budget.
    bool WasFlatteningLimited() const;

    //! Returns the revision incremented by actual path edits.
    std::uint64_t GetRevision() const;

    //! Returns the number of cache rebuilds, useful for performance diagnostics.
    std::size_t GetFlattenBuildCount() const;

  private:
    //! Supported immutable command payload interpretations.
    enum class Kind
    {
        Move,
        Line,
        Quadratic,
        Cubic,
        Arc,
        Close
    };

    //! One finite path instruction.
    struct Command
    {
        //! Selects the instruction.
        Kind m_Kind;

        //! Endpoints/control points or arc center.
        Point m_A, m_B, m_C;

        //! Arc radius and signed radian angles.
        float m_Radius = 0, m_Start = 0, m_Sweep = 0;
    };

    //! Appends a validated instruction and invalidates the flatten cache.
    void Append(Command command);

    //! Requires an active contour before adding an ordinary segment.
    void RequireContour() const;

    //! Bounded command sequence.
    std::vector<Command> m_Commands;

    //! Tracks whether ordinary segments have a starting point.
    bool m_HasContour = false;

    //! Monotonic source revision.
    std::uint64_t m_Revision = 0;

    //! Revision represented by the adaptive cache.
    mutable std::uint64_t m_CachedRevision = ~std::uint64_t{0};

    //! Tolerance represented by the adaptive cache.
    mutable float m_CachedTolerance = 0;

    //! Reusable flattened contours.
    mutable std::vector<PainterContour> m_Contours;

    //! Reports a quality budget limit without allowing unbounded subdivision.
    mutable bool m_Limited = false;

    //! Counts cache rebuilding rather than paint calls.
    mutable std::size_t m_BuildCount = 0;
};
} // namespace EStudioUI
