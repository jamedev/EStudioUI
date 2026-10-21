#include <EStudioUI/Widget.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
Widget::Widget(std::string type)
{
    std::transform(type.begin(), type.end(), type.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    m_Node.m_Type = std::move(type);
}

Widget::~Widget()
{
    m_Lifetime.reset();
}

void Widget::Attach(UiContext* context, Widget* parent)
{
    m_Context = context;
    m_Parent = parent;
    m_Node.m_Parent = parent ? &parent->m_Node : nullptr;
    for (auto& child : m_Children)
        child->Attach(context, this);
}

Widget& Widget::AddChild(std::unique_ptr<Widget> child)
{
    if (!child || child.get() == this || child->m_Parent)
        throw std::invalid_argument("A child must be non-null and have one owner.");
    child->Attach(m_Context, this);
    auto* result = child.get();
    m_Children.push_back(std::move(child));
    Invalidate();
    return *result;
}

bool Widget::RemoveChild(Widget& child)
{
    const auto found = std::find_if(m_Children.begin(), m_Children.end(),
                                    [&](const auto& pointer) { return pointer.get() == &child; });
    if (found == m_Children.end())
        return false;
    m_Children.erase(found);
    Invalidate();
    return true;
}

void Widget::ClearChildren()
{
    m_Children.clear();
    Invalidate();
}

const std::vector<std::unique_ptr<Widget>>& Widget::GetChildren() const
{
    return m_Children;
}

void Widget::SetId(std::string id)
{
    m_Node.m_Id = std::move(id);
    Invalidate();
}

const std::string& Widget::GetId() const
{
    return m_Node.m_Id;
}

void Widget::AddClass(std::string name)
{
    if (std::find(m_Node.m_Classes.begin(), m_Node.m_Classes.end(), name) == m_Node.m_Classes.end())
    {
        m_Node.m_Classes.push_back(std::move(name));
        Invalidate();
    }
}

void Widget::RemoveClass(const std::string& name)
{
    std::erase(m_Node.m_Classes, name);
    Invalidate();
}

void Widget::SetEnabled(bool enabled)
{
    m_Enabled = enabled;
    Invalidate();
}

bool Widget::IsEnabled() const
{
    return m_Enabled && (!m_Parent || m_Parent->IsEnabled());
}

void Widget::SetVisible(bool visible)
{
    if (m_Visible != visible)
    {
        m_Visible = visible;
        Invalidate();
    }
}

bool Widget::IsVisible() const
{
    return m_Visible && m_Style.m_Displayed && (!m_Parent || m_Parent->IsVisible());
}

void Widget::SetWidth(SizeRule rule)
{
    rule.Validate();
    m_WidthRule = rule;
    Invalidate();
}

void Widget::SetHeight(SizeRule rule)
{
    rule.Validate();
    m_HeightRule = rule;
    Invalidate();
}

void Widget::SetLayoutDirection(Axis axis)
{
    m_Direction = axis;
    Invalidate();
}

void Widget::SetGap(float pixels)
{
    if (!std::isfinite(pixels) || pixels < 0 || pixels > 1000000)
        throw std::invalid_argument("A layout gap must be finite and nonnegative.");
    m_Gap = pixels;
    Invalidate();
}

void Widget::SetFloatingBounds(Rect bounds)
{
    if (!std::isfinite(bounds.m_X) || !std::isfinite(bounds.m_Y) || !std::isfinite(bounds.m_Width) ||
        !std::isfinite(bounds.m_Height) || bounds.m_Width < 0 || bounds.m_Height < 0)
        throw std::invalid_argument("Floating bounds must be finite with nonnegative dimensions.");
    if (m_FloatingBounds && m_FloatingBounds->m_X == bounds.m_X && m_FloatingBounds->m_Y == bounds.m_Y &&
        m_FloatingBounds->m_Width == bounds.m_Width && m_FloatingBounds->m_Height == bounds.m_Height)
        return;
    m_FloatingBounds = bounds;
    Invalidate();
}

Widget* Widget::GetParent() const
{
    return m_Parent;
}

void Widget::ClearFloatingBounds()
{
    m_FloatingBounds.reset();
    Invalidate();
}

void Widget::SetStyleProperty(StyleProperty property, const StyleValue& value)
{
    Style validation;
    EStudioUI::SetStyleValue(validation, property, value);
    const auto validated = GetStyleValue(validation, property);
    const auto found = m_StyleOverrides.find(property);
    if (found != m_StyleOverrides.end() && found->second == validated)
        return;
    m_StyleOverrides[property] = validated;
    if (IsPaintOnlyProperty(property))
    {
        EStudioUI::SetStyleValue(m_Style, property, validated);
        RequestRepaint();
    }
    else
        Invalidate();
}

void Widget::ClearStyleProperty(StyleProperty property)
{
    if (m_StyleOverrides.erase(property))
        Invalidate();
}

void Widget::ClearStyleProperties()
{
    m_StyleOverrides.clear();
    Invalidate();
}

Signal<Point>& Widget::ContextMenuRequested()
{
    return m_ContextMenuRequested;
}

bool Widget::IsFocused() const
{
    return (m_Node.m_State & Focus) != 0;
}

bool Widget::IsHovered() const
{
    return (m_Node.m_State & Hover) != 0;
}

bool Widget::IsPressed() const
{
    return (m_Node.m_State & Pressed) != 0;
}

Rect Widget::GetBounds() const
{
    return m_Bounds;
}

Rect Widget::GetClip() const
{
    return m_Clip;
}

const Style& Widget::GetStyle() const
{
    return m_Style;
}

UiContext* Widget::GetContext() const
{
    return m_Context;
}

bool Widget::IsFocusable() const
{
    return false;
}

Rect Widget::GetContentBounds() const
{
    const auto& padding = m_Style.m_Padding;
    const auto border = m_Style.m_BorderWidth;
    return {m_Bounds.m_X + padding.m_Left + border, m_Bounds.m_Y + padding.m_Top + border,
            std::max(0.0f, m_Bounds.m_Width - padding.m_Left - padding.m_Right - 2 * border),
            std::max(0.0f, m_Bounds.m_Height - padding.m_Top - padding.m_Bottom - 2 * border)};
}

Widget* Widget::FindById(const std::string& id)
{
    if (m_Node.m_Id == id)
        return this;
    for (const auto& child : m_Children)
        if (auto* found = child->FindById(id))
            return found;
    return nullptr;
}

Point Widget::Measure(FontFace&) const
{
    return {};
}

void Widget::Paint(Renderer& renderer, FontFace&, double, float opacity) const
{
    renderer.DrawBox(m_Bounds, m_Style, m_Clip, opacity);
}

void Widget::OnPointerDown(const PointerEvent&)
{
}

void Widget::OnPointerMove(const PointerEvent&)
{
}

void Widget::OnPointerUp(const PointerEvent&)
{
}

void Widget::OnKey(Key, bool, bool, Modifiers)
{
}

void Widget::OnText(std::string_view)
{
}

void Widget::OnFocus(bool)
{
}

void Widget::OnCaptureLost()
{
}

void Widget::OnLayout()
{
}

bool Widget::OnScroll(Point, Modifiers)
{
    return false;
}

bool Widget::OnKeyPreview(Key, bool, bool, Modifiers)
{
    return false;
}

void Widget::PrepareFrame(double, float, float)
{
}

bool Widget::NeedsContinuousRendering() const
{
    return false;
}

void Widget::FrameSubmitted()
{
}

void Widget::Invalidate()
{
    if (m_Context)
        m_Context->Invalidate();
}

void Widget::RequestRepaint()
{
    if (m_Context)
        m_Context->RequestRepaint();
}

void Widget::SetState(unsigned flag, bool enabled)
{
    const auto state = enabled ? m_Node.m_State | flag : m_Node.m_State & ~flag;
    if (state != m_Node.m_State)
    {
        m_Node.m_State = state;
        Invalidate();
    }
}

WidgetRef::WidgetRef(Widget* widget) : m_Widget(widget)
{
    if (widget)
        m_Lifetime = widget->m_Lifetime;
}

Widget* WidgetRef::Get() const
{
    return m_Lifetime.expired() ? nullptr : m_Widget;
}
} // namespace EStudioUI
