#pragma once

#include "Widget.h"

#include <memory>

namespace EStudioUI
{
//! Compiled CSS state transitions and keyframe timelines for widgets and paint layers.
class CssMotion
{
  public:
    //! Connects a GUI context without owning it.
    explicit CssMotion(UiContext& context);

    //! Releases cached styles and weak target references.
    ~CssMotion();

    //! Resets timelines after a stylesheet replacement.
    void Reset();

    //! Updates a target's underlying style and returns its current presentation.
    const Style& Resolve(Widget& widget, int layer, Style base, const StyleSheet& sheet);

    //! Advances cached tracks without evaluating CSS selectors or layout.
    void Update(double time);

    //! Returns a current decorative layer or null.
    const Style* Decoration(const Widget& widget, int layer) const;

    //! Reports active visible timelines; paused and completed tracks do not request frames.
    bool HasRunning() const;

  private:
    //! Keeps compiled tracks out of public headers.
    struct Impl;

    //! Owns weak target state and reusable presentation buffers.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
