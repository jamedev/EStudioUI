#include <EStudioUI/Widget.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace EStudioUI
{
Widget::Widget(std::string type) : m_TargetKey(UiDispatcher::AllocateTargetKey())
{
    std::transform(type.begin(), type.end(), type.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    m_Node.m_Type = std::move(type);
}

Widget::~Widget()
{
    if (!IsOwnerThread())
        std::terminate();
    if (m_Context)
        m_Context->ReleaseSelectorOwner(*this);
    m_Lifetime.reset();
    m_LifetimeStop.request_stop();
}

void Widget::VerifyAccess() const
{
    m_Affinity.VerifyAccess("Widget access");
}

bool Widget::IsOwnerThread() const noexcept
{
    return m_Affinity.IsOwnerThread();
}

void Widget::VerifyStructuralMutation() const
{
    VerifyAccess();
    if (m_Context)
        m_Context->VerifyStructuralMutation();
}

std::shared_ptr<void> Widget::RetainOwner() const
{
    VerifyAccess();
    return m_Context ? m_Context->RetainOwner() : std::shared_ptr<void>{};
}

void Widget::Attach(UiContext* context, Widget* parent)
{
    VerifyAccess();
    if (context)
        context->VerifyAccess();
    if (parent)
        parent->VerifyAccess();
    m_Context = context;
    m_Parent = parent;
    m_Node.m_Parent = parent ? &parent->m_Node : nullptr;
    for (auto& child : m_Children)
        child->Attach(context, this);
}

Widget& Widget::AddChild(std::unique_ptr<Widget> child)
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    if (child)
        child->VerifyAccess();
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
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    const auto found = std::find_if(m_Children.begin(), m_Children.end(),
                                    [&](const auto& pointer) { return pointer.get() == &child; });
    if (found == m_Children.end())
        return false;
    auto removed = std::move(*found);
    m_Children.erase(found);
    Invalidate();
    removed.reset();
    return true;
}

void Widget::ClearChildren()
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    auto removed = std::move(m_Children);
    Invalidate();
    removed.clear();
}

const std::vector<std::unique_ptr<Widget>>& Widget::GetChildren() const
{
    VerifyAccess();
    return m_Children;
}

void Widget::SetId(std::string id)
{
    VerifyAccess();
    m_Node.m_Id = std::move(id);
    Invalidate();
}

const std::string& Widget::GetId() const
{
    VerifyAccess();
    return m_Node.m_Id;
}

void Widget::AddClass(std::string name)
{
    VerifyAccess();
    if (std::find(m_Node.m_Classes.begin(), m_Node.m_Classes.end(), name) == m_Node.m_Classes.end())
    {
        m_Node.m_Classes.push_back(std::move(name));
        Invalidate();
    }
}

void Widget::RemoveClass(const std::string& name)
{
    VerifyAccess();
    std::erase(m_Node.m_Classes, name);
    Invalidate();
}

void Widget::SetEnabled(bool enabled)
{
    VerifyAccess();
    m_Enabled = enabled;
    Invalidate();
}

bool Widget::IsEnabled() const
{
    VerifyAccess();
    return m_Enabled && (!m_Parent || m_Parent->IsEnabled());
}

void Widget::SetVisible(bool visible)
{
    VerifyAccess();
    if (m_Visible != visible)
    {
        m_Visible = visible;
        Invalidate();
    }
}

bool Widget::IsVisible() const
{
    VerifyAccess();
    return m_Visible && m_Style.m_Displayed && (!m_Parent || m_Parent->IsVisible());
}

void Widget::SetWidth(SizeRule rule)
{
    VerifyAccess();
    rule.Validate();
    m_WidthRule = rule;
    Invalidate();
}

void Widget::SetHeight(SizeRule rule)
{
    VerifyAccess();
    rule.Validate();
    m_HeightRule = rule;
    Invalidate();
}

void Widget::SetLayoutDirection(Axis axis)
{
    VerifyAccess();
    m_Direction = axis;
    Invalidate();
}

void Widget::SetGap(float pixels)
{
    VerifyAccess();
    if (!std::isfinite(pixels) || pixels < 0 || pixels > 1000000)
        throw std::invalid_argument("A layout gap must be finite and nonnegative.");
    m_Gap = pixels;
    Invalidate();
}

void Widget::SetFloatingBounds(Rect bounds)
{
    VerifyAccess();
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
    VerifyAccess();
    return m_Parent;
}

void Widget::ClearFloatingBounds()
{
    VerifyAccess();
    m_FloatingBounds.reset();
    Invalidate();
}

void Widget::SetStyleProperty(StyleProperty property, const StyleValue& value)
{
    VerifyAccess();
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
        if (m_Context)
            m_Context->RefreshStyleParts(*this);
        else
            for (auto& part : m_StyleParts)
            {
                for (std::size_t i = 0; i < part.m_States.size(); ++i)
                {
                    part.m_Palette[i] = part.m_Defaults(m_Style, part.m_States[i]);
                    if (!part.m_Animated)
                    {
                        part.m_Palette[i].m_Transitions.clear();
                        part.m_Palette[i].m_Animations.clear();
                    }
                }
                part.m_Presentation = part.m_Palette[PartIndex(part.m_StateMask, part.m_ActiveState)];
            }
        RequestRepaint();
    }
    else
        Invalidate();
}

void Widget::ClearStyleProperty(StyleProperty property)
{
    VerifyAccess();
    if (m_StyleOverrides.erase(property))
        Invalidate();
}

void Widget::ClearStyleProperties()
{
    VerifyAccess();
    m_StyleOverrides.clear();
    for (auto& part : m_StyleParts)
        for (auto& overrides : part.m_Overrides)
            overrides.clear();
    Invalidate();
}

Signal<Point>& Widget::ContextMenuRequested()
{
    VerifyAccess();
    return m_ContextMenuRequested;
}

bool Widget::IsFocused() const
{
    VerifyAccess();
    return (m_Node.m_State & Focus) != 0;
}

bool Widget::IsHovered() const
{
    VerifyAccess();
    return (m_Node.m_State & Hover) != 0;
}

bool Widget::IsPressed() const
{
    VerifyAccess();
    return (m_Node.m_State & Pressed) != 0;
}

Rect Widget::GetBounds() const
{
    VerifyAccess();
    return m_Bounds;
}

Rect Widget::GetClip() const
{
    VerifyAccess();
    return m_Clip;
}

const Style& Widget::GetStyle() const
{
    VerifyAccess();
    return m_Style;
}

UiContext* Widget::GetContext() const
{
    VerifyAccess();
    return m_Context;
}

bool Widget::IsFocusable() const
{
    VerifyAccess();
    return false;
}

Rect Widget::GetContentBounds() const
{
    VerifyAccess();
    const auto& padding = m_Style.m_Padding;
    const auto border = m_Style.m_BorderWidth;
    return {m_Bounds.m_X + padding.m_Left + border, m_Bounds.m_Y + padding.m_Top + border,
            std::max(0.0f, m_Bounds.m_Width - padding.m_Left - padding.m_Right - 2 * border),
            std::max(0.0f, m_Bounds.m_Height - padding.m_Top - padding.m_Bottom - 2 * border)};
}

Widget* Widget::FindById(const std::string& id)
{
    VerifyAccess();
    if (m_Node.m_Id == id)
        return this;
    for (const auto& child : m_Children)
        if (auto* found = child->FindById(id))
            return found;
    return nullptr;
}

Point Widget::Measure(FontFace&) const
{
    VerifyAccess();
    return {};
}

void Widget::Paint(Renderer& renderer, FontFace&, double, float opacity) const
{
    VerifyAccess();
    renderer.DrawBox(m_Bounds, m_Style, m_Clip, opacity);
}

void Widget::OnPointerDown(const PointerEvent&)
{
    VerifyAccess();
}

void Widget::OnPointerMove(const PointerEvent&)
{
    VerifyAccess();
}

void Widget::OnPointerUp(const PointerEvent&)
{
    VerifyAccess();
}

void Widget::OnKey(Key, bool, bool, Modifiers)
{
    VerifyAccess();
}

void Widget::OnText(std::string_view)
{
    VerifyAccess();
}

void Widget::OnFocus(bool)
{
    VerifyAccess();
}

void Widget::OnCaptureLost()
{
    VerifyAccess();
}

void Widget::OnLayout()
{
    VerifyAccess();
}

bool Widget::OnScroll(Point, Modifiers)
{
    VerifyAccess();
    return false;
}

bool Widget::OnKeyPreview(Key, bool, bool, Modifiers)
{
    VerifyAccess();
    return false;
}

void Widget::PrepareFrame(double, float, float)
{
    VerifyAccess();
}

bool Widget::NeedsContinuousRendering() const
{
    VerifyAccess();
    return false;
}

void Widget::FrameSubmitted()
{
    VerifyAccess();
}

void Widget::Invalidate()
{
    VerifyAccess();
    if (m_Context)
        m_Context->Invalidate();
}

void Widget::RequestRepaint()
{
    VerifyAccess();
    if (m_Context)
        m_Context->RequestRepaint();
}

std::size_t Widget::PartIndex(unsigned mask, unsigned state)
{
    std::size_t result = 0, denseBit = 1;
    while (mask)
    {
        const unsigned bit = mask & (~mask + 1);
        if (state & bit)
            result |= denseBit;
        denseBit <<= 1;
        mask &= ~bit;
    }
    return result;
}

void Widget::RegisterStylePart(std::string name, unsigned stateMask,
                               std::function<Style(const Style&, unsigned)> defaults, bool animated)
{
    VerifyAccess();
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (name.empty() || name == "before" || name == "after" || !defaults || m_StyleParts.size() >= 32 ||
        std::popcount(stateMask) > 6 ||
        !std::all_of(name.begin(), name.end(),
                     [](unsigned char value) { return std::isalnum(value) || value == '-' || value == '_'; }) ||
        std::any_of(m_StyleParts.begin(), m_StyleParts.end(), [&](const auto& part) { return part.m_Name == name; }))
        throw std::invalid_argument(
            "A paint part requires a unique name, defaults, and at most six varying states (32 parts maximum).");
    StylePart part;
    part.m_Name = std::move(name);
    part.m_StateMask = stateMask;
    part.m_Animated = animated;
    part.m_Defaults = std::move(defaults);
    unsigned state = 0;
    do
    {
        part.m_States.push_back(state);
        auto style = part.m_Defaults(m_Style, state);
        if (!animated)
        {
            style.m_Transitions.clear();
            style.m_Animations.clear();
        }
        part.m_Palette.push_back(std::move(style));
        part.m_Assignments.emplace_back();
        part.m_Overrides.emplace_back();
        state = (state - stateMask) & stateMask;
    } while (state);
    part.m_Presentation = part.m_Palette.front();
    m_StyleParts.push_back(std::move(part));
    Invalidate();
}

const Style& Widget::GetPartStyle(std::string_view name) const
{
    VerifyAccess();
    const auto found =
        std::find_if(m_StyleParts.begin(), m_StyleParts.end(), [&](const auto& part) { return part.m_Name == name; });
    if (found == m_StyleParts.end())
        throw std::out_of_range("Unregistered paint part.");
    return found->m_Animated ? found->m_Presentation : found->m_Palette[PartIndex(found->m_StateMask, found->m_State)];
}

const Style& Widget::GetPartStyle(std::string_view name, unsigned state) const
{
    VerifyAccess();
    const auto found =
        std::find_if(m_StyleParts.begin(), m_StyleParts.end(), [&](const auto& part) { return part.m_Name == name; });
    if (found == m_StyleParts.end())
        throw std::out_of_range("Unregistered paint part.");
    state &= found->m_StateMask;
    if (found->m_Animated && state == (found->m_ActiveState & found->m_StateMask))
        return found->m_Presentation;
    return found->m_Palette[PartIndex(found->m_StateMask, state)];
}

void Widget::SetPartState(std::string_view name, unsigned state)
{
    VerifyAccess();
    const auto found =
        std::find_if(m_StyleParts.begin(), m_StyleParts.end(), [&](const auto& part) { return part.m_Name == name; });
    if (found == m_StyleParts.end())
        throw std::out_of_range("Unregistered paint part.");
    state &= found->m_StateMask;
    if (found->m_State == state)
        return;
    found->m_State = state;
    if (!m_Context)
    {
        found->m_ActiveState = state;
        found->m_Presentation = found->m_Palette[PartIndex(found->m_StateMask, state)];
    }
    Invalidate();
}

void Widget::SetState(unsigned flag, bool enabled)
{
    VerifyAccess();
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
    {
        widget->VerifyAccess();
        m_OwnerThread = widget->m_Affinity.GetOwnerThreadId();
        m_Lifetime = widget->m_Lifetime;
    }
}

Widget* WidgetRef::Get() const
{
    if (!m_Widget)
        return nullptr;
    ThreadAffinity::VerifyThread(m_OwnerThread, "WidgetRef::Get");
    return m_Lifetime.expired() ? nullptr : m_Widget;
}
} // namespace EStudioUI
