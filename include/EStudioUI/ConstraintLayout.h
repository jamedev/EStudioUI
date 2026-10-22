#pragma once

#include "ConstraintItem.h"
#include "LayoutStrategy.h"

#include <memory>

namespace EStudioUI
{
//! Cumulative, owned work counters for a single constraint strategy instance.
struct ConstraintLayoutStats
{
    //! Tableaux built after topology changes or infeasibility recovery.
    std::size_t m_Builds = 0;
    //! Existing tableaux updated without rebuilding their unchanged rows.
    std::size_t m_Updates = 0;
    //! Solves performed, including width-first height resolution.
    std::size_t m_Solves = 0;
    //! Identical row sets reused without another solve.
    std::size_t m_CacheHits = 0;
};

//! Optional linear anchor layout with bounded incremental solver caches and owned diagnostics.
class ConstraintLayout final : public LayoutStrategy
{
  public:
    //! Creates an unconstrained container with content preferences and soft containment.
    ConstraintLayout();
    //! Releases private solver state.
    ~ConstraintLayout() override;
    //! Copies rule definitions, starting with fresh solver caches.
    ConstraintLayout(const ConstraintLayout& other);
    //! Replaces definitions and resets solver caches.
    ConstraintLayout& operator=(const ConstraintLayout& other);
    //! Transfers definitions and caches into a widget.
    ConstraintLayout(ConstraintLayout&& other) noexcept;
    //! Transfers definitions and caches.
    ConstraintLayout& operator=(ConstraintLayout&& other) noexcept;
    //! Adds a named rule in deterministic conflict-resolution order, before SetLayout.
    ConstraintLayout& Add(Constraint constraint, ConstraintStrength strength = ConstraintStrength::Required,
                          std::string name = {});
    //! Returns Constraints for allocation reports.
    std::string_view GetName() const override;
    //! Solves widths, measures dependent heights, then solves vertically without width feedback.
    LayoutPlan Compute(const LayoutContext& context) const override;
    //! Returns cumulative work counters without borrowing solver storage.
    ConstraintLayoutStats GetStats() const;

  private:
    //! Keeps solver types and mutable bounded caches out of the public interface.
    struct Impl;
    //! Owned configuration and at most three pairs of solver tableaux.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
