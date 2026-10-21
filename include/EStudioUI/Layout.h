#pragma once

#include <vector>

namespace EStudioUI
{
//! The flow axis of a container.
enum class Axis
{
    Horizontal,
    Vertical
};

//! One measured item passed to the deterministic space allocator.
struct LayoutItem
{
    //! Initial outer size before distributing free space.
    float m_Basis = 0;

    //! Hard lower limit.
    float m_Minimum = 0;

    //! Hard upper limit.
    float m_Maximum = 1000000;

    //! Positive growth weight, or zero to preserve content/fixed size.
    float m_Weight = 0;
};

//! Allocated sizes and explicit unused/overflow diagnostics.
struct LayoutResult
{
    //! Final sizes in input order.
    std::vector<float> m_Sizes;

    //! Space left after every growing child reaches its maximum.
    float m_Unused = 0;

    //! Space required beyond the container; hard minimums are never violated.
    float m_Overflow = 0;
};

//! A platform-independent allocator shared by widget layouts and table columns.
class Layout
{
  public:
    //! Distributes available space, redistributing shares from capped items.
    static LayoutResult Distribute(float available, const std::vector<LayoutItem>& items);
};
} // namespace EStudioUI
