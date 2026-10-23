#include <EStudioUI/Splitter.h>
#include <EStudioUI/BoxLayout.h>
#include <EStudioUI/Renderer.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace EStudioUI
{
namespace Detail
{
struct SplitterData
{
    Axis m_Axis = Axis::Horizontal;
    std::vector<float> m_Request;
    std::unordered_map<std::uint64_t, float> m_Weights;
};
} // namespace Detail

namespace
{
class SplitLayout final : public LayoutStrategy
{
  public:
    SplitLayout(std::shared_ptr<Detail::SplitterData> data, std::function<float()> gap)
        : m_Data(std::move(data)), m_Gap(std::move(gap))
    {
    }

    std::string_view GetName() const override
    {
        return "Splitter";
    }

    LayoutPlan Compute(const LayoutContext& source) const override
    {
        auto c = source;
        c.m_Gap = m_Gap();
        for (std::size_t i = 0; i < c.m_Children.size(); ++i)
        {
            auto& length = m_Data->m_Axis == Axis::Horizontal ? c.m_Children[i].m_Width : c.m_Children[i].m_Height;
            if (length.m_Unit != Length::Unit::Pixels)
            {
                const auto found = m_Data->m_Weights.find(c.m_Children[i].m_Target);
                length = {Length::Unit::Fraction, found == m_Data->m_Weights.end() ? 1 : found->second};
            }
        }
        return BoxLayout(m_Data->m_Axis).Compute(c);
    }

  private:
    std::shared_ptr<Detail::SplitterData> m_Data;
    std::function<float()> m_Gap;
};
} // namespace

Splitter::Splitter(Axis axis) : Widget("splitter"), m_Data(std::make_shared<Detail::SplitterData>())
{
    RegisterStylePart("handle", Horizontal | Vertical | Hover | Pressed | Selected,
                      [](const Style&, unsigned)
                      {
                          Style s;
                          s.m_Width = s.m_Height = {Length::Unit::Pixels, 8};
                          s.m_Radius = 3;
                          s.m_Background = {0.18f, 0.25f, 0.38f, 1};
                          return s;
                      });
    SetLayout(SplitLayout(
        m_Data,
        [this]
        {
            const auto& s = GetPartStyle("handle", m_Data->m_Axis == Axis::Horizontal ? Horizontal : Vertical);
            return s.m_Displayed
                       ? std::max(0.0f, (m_Data->m_Axis == Axis::Horizontal ? s.m_Width : s.m_Height).Resolve(8, 8))
                       : 0;
        }));
    SetOrientation(axis);
}

void Splitter::SetOrientation(Axis axis)
{
    VerifyStructuralMutation();
    if (axis != Axis::Horizontal && axis != Axis::Vertical)
        throw std::invalid_argument("Invalid splitter orientation.");
    m_Data->m_Axis = axis;
    SetState(Horizontal, axis == Axis::Horizontal);
    SetState(Vertical, axis == Axis::Vertical);
    Invalidate(Invalidation::Content);
}

Axis Splitter::GetOrientation() const
{
    VerifyAccess();
    return m_Data->m_Axis;
}

bool Splitter::IsFocusable() const
{
    VerifyAccess();
    return true;
}

void Splitter::SetSizes(std::vector<float> sizes)
{
    VerifyStructuralMutation();
    const auto count = GetChildren().size();
    if (sizes.size() != count || sizes.size() > 4096 ||
        std::any_of(sizes.begin(), sizes.end(),
                    [](float size) { return !std::isfinite(size) || size <= 0 || size > 1000000; }))
        throw std::invalid_argument(
            "Splitter sizes require one positive finite value per displayed pane (maximum 4,096).");
    if (sizes == m_Data->m_Request)
        return;
    const auto lease = RetainOwner();
    std::unordered_map<std::uint64_t, float> weights;
    for (std::size_t i = 0; i < sizes.size(); ++i)
        weights.emplace(GetChildren()[i]->m_TargetKey, sizes[i]);
    m_Data->m_Request = sizes;
    m_Data->m_Weights = std::move(weights);
    Invalidate(Invalidation::Measure | Invalidation::Arrange | Invalidation::Paint);
    NotifySemanticsChanged();
    m_SizesChanged.Emit(sizes);
}

std::vector<float> Splitter::GetSizes() const
{
    VerifyAccess();
    std::unordered_map<std::uint64_t, float> prepared;
    for (const auto& child : GetPreparedLayoutReport().m_Children)
        prepared.emplace(child.m_Child, m_Data->m_Axis == Axis::Horizontal ? child.m_Slot.m_Bounds.m_Width
                                                                           : child.m_Slot.m_Bounds.m_Height);
    std::vector<float> result;
    for (const auto& child : GetChildren())
    {
        const auto found = prepared.find(child->m_TargetKey);
        const auto stored = m_Data->m_Weights.find(child->m_TargetKey);
        result.push_back(found != prepared.end()             ? found->second
                         : stored != m_Data->m_Weights.end() ? stored->second
                                                             : 1);
    }
    return result;
}

Rect Splitter::GetHandleBounds(std::size_t index) const
{
    VerifyAccess();
    const auto& report = GetPreparedLayoutReport();
    if (index + 1 >= report.m_Children.size())
        return {};
    const auto a = report.m_Children[index].m_Slot.m_Bounds, b = report.m_Children[index + 1].m_Slot.m_Bounds,
               c = GetContentBounds();
    if (m_Data->m_Axis == Axis::Horizontal)
        return {c.m_X + a.m_X + a.m_Width, c.m_Y, std::max(0.0f, b.m_X - a.m_X - a.m_Width), c.m_Height};
    return {c.m_X, c.m_Y + a.m_Y + a.m_Height, c.m_Width, std::max(0.0f, b.m_Y - a.m_Y - a.m_Height)};
}

Signal<const std::vector<float>&>& Splitter::SizesChanged()
{
    VerifyAccess();
    return m_SizesChanged;
}

unsigned Splitter::HandleState(std::size_t index) const
{
    return (m_Data->m_Axis == Axis::Horizontal ? Horizontal : Vertical) |
           (m_Hover == static_cast<int>(index) ? Hover : 0u) | (m_Dragging && m_Handle == index ? Pressed : 0u) |
           (IsFocused() && m_Handle == index ? Selected : 0u);
}

int Splitter::HandleAt(Point position) const
{
    const auto count = GetPreparedLayoutReport().m_Children.size();
    for (std::size_t i = 0; i + 1 < count; ++i)
    {
        const auto bounds = GetHandleBounds(i);
        const auto& style = GetPartStyle("handle", HandleState(i));
        Affine inverse;
        if (style.m_Displayed && bounds.Contains(position) && style.m_Transform.Resolve(bounds).Inverse(inverse) &&
            ContainsRounded(bounds, style.m_Radius, inverse.Map(position)))
            return static_cast<int>(i);
    }
    return -1;
}

void Splitter::Paint(Renderer& renderer, FontFace&, double, float opacity) const
{
    renderer.DrawBox(GetBounds(), GetStyle(), GetClip(), opacity);
    const auto count = GetPreparedLayoutReport().m_Children.size();
    for (std::size_t i = 0; i + 1 < count; ++i)
    {
        const auto& style = GetPartStyle("handle", HandleState(i));
        const auto bounds = GetHandleBounds(i);
        renderer.PushState();
        renderer.PushClip(bounds);
        renderer.PushState(style.m_Transform.Resolve(bounds));
        renderer.DrawBox(bounds, style, GetClip(), opacity * style.m_Opacity);
        renderer.PopState();
        renderer.PopState();
    }
}

bool Splitter::OnInputPreview(const InputEvent& event)
{
    if (const auto* pointer = std::get_if<PointerEvent>(&event);
        pointer && pointer->m_Type == PointerEventType::Move && !m_Dragging)
    {
        const int hover = HandleAt(pointer->m_Position);
        if (hover != m_Hover)
        {
            m_Hover = hover;
            SetCursor(hover < 0 ? std::nullopt
                                : std::optional(m_Data->m_Axis == Axis::Horizontal ? CursorShape::ResizeHorizontal
                                                                                   : CursorShape::ResizeVertical));
            RequestRepaint();
        }
    }
    return false;
}

void Splitter::OnPointerDown(const PointerEvent& event)
{
    const int handle = HandleAt(event.m_Position);
    if (handle < 0)
        return;
    m_Handle = static_cast<std::size_t>(handle);
    m_Dragging = true;
    m_DragPosition = m_Data->m_Axis == Axis::Horizontal ? event.m_Position.m_X : event.m_Position.m_Y;
    RequestRepaint();
}

void Splitter::MoveHandle(float delta)
{
    const auto& report = GetPreparedLayoutReport();
    std::vector<float> sizes;
    for (const auto& item : report.m_Children)
        sizes.push_back(m_Data->m_Axis == Axis::Horizontal ? item.m_Slot.m_Bounds.m_Width
                                                           : item.m_Slot.m_Bounds.m_Height);
    if (m_Handle + 1 >= sizes.size())
        return;
    const auto& a = report.m_Children[m_Handle];
    const auto& b = report.m_Children[m_Handle + 1];
    const bool horizontal = m_Data->m_Axis == Axis::Horizontal;
    const auto& al = horizontal ? a.m_Rules.m_Width : a.m_Rules.m_Height;
    const auto& bl = horizontal ? b.m_Rules.m_Width : b.m_Rules.m_Height;
    if (al.m_Unit == Length::Unit::Pixels || bl.m_Unit == Length::Unit::Pixels)
        return;
    const float amin = horizontal ? a.m_Rules.m_Minimum.m_X : a.m_Rules.m_Minimum.m_Y,
                bmin = horizontal ? b.m_Rules.m_Minimum.m_X : b.m_Rules.m_Minimum.m_Y;
    const float amax = horizontal ? a.m_Rules.m_Maximum.m_X : a.m_Rules.m_Maximum.m_Y,
                bmax = horizontal ? b.m_Rules.m_Maximum.m_X : b.m_Rules.m_Maximum.m_Y;
    const float low = std::max(amin - sizes[m_Handle], sizes[m_Handle + 1] - bmax),
                high = std::min(amax - sizes[m_Handle], sizes[m_Handle + 1] - bmin);
    if (low > high)
        return;
    delta = std::clamp(delta, low, high);
    sizes[m_Handle] = std::max(0.001f, sizes[m_Handle] + delta);
    sizes[m_Handle + 1] = std::max(0.001f, sizes[m_Handle + 1] - delta);
    auto requested = GetSizes();
    for (std::size_t i = 0; i < report.m_Children.size(); ++i)
    {
        const auto& item = report.m_Children[i];
        const auto minimum = horizontal ? item.m_Rules.m_Minimum.m_X : item.m_Rules.m_Minimum.m_Y;
        const auto found = std::find_if(GetChildren().begin(), GetChildren().end(),
                                        [&](const auto& child) { return child->m_TargetKey == item.m_Child; });
        if (found != GetChildren().end())
            requested[static_cast<std::size_t>(found - GetChildren().begin())] = std::max(0.001f, sizes[i] - minimum);
    }
    SetSizes(std::move(requested));
}

void Splitter::OnPointerMove(const PointerEvent& event)
{
    if (!m_Dragging)
        return;
    const float position = m_Data->m_Axis == Axis::Horizontal ? event.m_Position.m_X : event.m_Position.m_Y;
    const float delta = position - m_DragPosition;
    m_DragPosition = position;
    MoveHandle(delta);
}

void Splitter::OnPointerUp(const PointerEvent&)
{
    OnCaptureLost();
}

void Splitter::OnCaptureLost()
{
    m_Dragging = false;
    m_Hover = -1;
    SetCursor({});
    RequestRepaint();
}

void Splitter::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Control || modifiers.m_Alt)
        return;
    const auto count = GetPreparedLayoutReport().m_Children.size();
    if (count < 2)
        return;
    if (key == Key::Home)
    {
        m_Handle = 0;
        RequestRepaint();
    }
    else if (key == Key::End)
    {
        m_Handle = count - 2;
        RequestRepaint();
    }
    else if ((m_Data->m_Axis == Axis::Horizontal && (key == Key::Left || key == Key::Right)) ||
             (m_Data->m_Axis == Axis::Vertical && (key == Key::Up || key == Key::Down)))
        MoveHandle((key == Key::Left || key == Key::Up ? -1.0f : 1.0f) * (modifiers.m_Shift ? 1 : 10));
}

SemanticInfo Splitter::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Role = SemanticRole::Splitter;
    info.m_Minimum = 0;
    info.m_Maximum = 100;
    info.m_Step = 1;
    std::vector<float> sizes;
    for (const auto& item : GetPreparedLayoutReport().m_Children)
        sizes.push_back(m_Data->m_Axis == Axis::Horizontal ? item.m_Slot.m_Bounds.m_Width
                                                           : item.m_Slot.m_Bounds.m_Height);
    if (m_Handle + 1 < sizes.size())
    {
        const double span = sizes[m_Handle] + sizes[m_Handle + 1];
        info.m_Number = span > 0 ? sizes[m_Handle] * 100.0 / span : 0;
        info.m_Actions = SemanticAction::SetNumber;
    }
    return info;
}

SemanticStatus Splitter::OnSemanticAction(const SemanticCommand& command)
{
    if (command.m_Action != SemanticAction::SetNumber)
        return SemanticStatus::Unsupported;
    if (!std::isfinite(command.m_Number) || command.m_Number < 0 || command.m_Number > 100)
        return SemanticStatus::InvalidValue;
    std::vector<float> sizes;
    for (const auto& item : GetPreparedLayoutReport().m_Children)
        sizes.push_back(m_Data->m_Axis == Axis::Horizontal ? item.m_Slot.m_Bounds.m_Width
                                                           : item.m_Slot.m_Bounds.m_Height);
    if (m_Handle + 1 >= sizes.size())
        return SemanticStatus::Unavailable;
    MoveHandle(
        static_cast<float>((sizes[m_Handle] + sizes[m_Handle + 1]) * command.m_Number / 100.0 - sizes[m_Handle]));
    return SemanticStatus::Succeeded;
}
} // namespace EStudioUI
