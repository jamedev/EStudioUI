#pragma once
#include "Widget.h"

namespace EStudioUI
{
namespace Detail
{
struct SplitterData;
}

//! User-resizable panes sharing the bounded box allocator and hard size contracts.
class Splitter final : public Widget
{
  public:
    //! Creates horizontal panes; Add creates ordinary pane children.
    explicit Splitter(Axis axis = Axis::Horizontal);
    //! Changes pane orientation.
    void SetOrientation(Axis axis);
    //! Returns pane orientation.
    Axis GetOrientation() const;
    //! Restores positive stretch weights for every owned pane, including hidden panes.
    void SetSizes(std::vector<float> sizes);
    //! Returns allocated pane sizes; hidden panes retain their positive stored weight.
    std::vector<float> GetSizes() const;
    //! Returns a prepared handle rectangle, or empty for an invalid handle.
    Rect GetHandleBounds(std::size_t index) const;
    //! Reports committed drag, keyboard and programmatic positions.
    Signal<const std::vector<float>&>& SizesChanged();
    //! Participates in keyboard navigation.
    bool IsFocusable() const override;

  protected:
    //! Supplies a range for the current handle.
    SemanticInfo DescribeSemantics() const override;
    //! Applies a native range update through normal sizing.
    SemanticStatus OnSemanticAction(const SemanticCommand& command) override;
    //! Paints rich shared handle styles.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;
    //! Updates handle hover before child delivery.
    bool OnInputPreview(const InputEvent& event) override;
    //! Begins a captured handle drag.
    void OnPointerDown(const PointerEvent& event) override;
    //! Updates a captured handle.
    void OnPointerMove(const PointerEvent& event) override;
    //! Ends a captured drag.
    void OnPointerUp(const PointerEvent& event) override;
    //! Cancels a drag and pointer cursor.
    void OnCaptureLost() override;
    //! Moves a handle with arrows, or selects a handle with Home/End.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

  private:
    //! Resolves a handle at a layout point.
    int HandleAt(Point position) const;
    //! Applies a two-pane size delta while preserving hard limits.
    void MoveHandle(float delta);
    //! Returns one handle's complete state.
    unsigned HandleState(std::size_t index) const;
    //! Shares immutable reads with the pure layout strategy.
    std::shared_ptr<Detail::SplitterData> m_Data;
    //! Current hovered handle, or minus one.
    int m_Hover = -1;
    //! Current keyboard/drag handle.
    std::size_t m_Handle = 0;
    //! Stores the previous captured coordinate.
    float m_DragPosition = 0;
    //! Enables captured movement.
    bool m_Dragging = false;
    //! Publishes an owned local snapshot during notification.
    Signal<const std::vector<float>&> m_SizesChanged;
};
} // namespace EStudioUI
