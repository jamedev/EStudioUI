#include <EStudioUI/Widget.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/CssMotion.h>
#include <EStudioUI/Action.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/ClipboardData.h>

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
    {
        if (!m_Actions.empty())
            m_Context->UnregisterActions(*this);
        m_Context->ReleaseSelectorOwner(*this);
        if (m_Context->m_Semantics)
            m_Context->m_Semantics->Unregister(*this);
    }
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
    if (m_Context && !m_Actions.empty())
        m_Context->UnregisterActions(*this);
    if (m_Context && m_Context->m_Semantics)
        m_Context->m_Semantics->Unregister(*this);
    m_Context = context;
    if (m_Context && m_Context->m_Semantics)
        m_Context->m_Semantics->Register(*this);
    m_Parent = parent;
    m_StyleDirty = m_SubtreeStyleDirty = m_DescendantStyleDirty = m_ArrangeDirty = true;
    m_IntrinsicValid = m_MeasureValid = false;
    m_Node.m_Parent = parent ? &parent->m_Node : nullptr;
    if (m_Context)
        for (const auto& association : m_Actions)
            m_Context->RegisterAction(*this, association.m_Action, association.m_Scope);
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
    Invalidate(Invalidation::Structure);
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
    if (m_BaselineChild == child.m_TargetKey)
        m_BaselineChild = 0;
    auto removed = std::move(*found);
    m_Children.erase(found);
    Invalidate(Invalidation::Structure);
    removed.reset();
    return true;
}

void Widget::ClearChildren()
{
    VerifyStructuralMutation();
    const auto ownerLease = RetainOwner();
    auto removed = std::move(m_Children);
    m_BaselineChild = 0;
    Invalidate(Invalidation::Structure);
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
    if (m_Node.m_Id == id)
        return;
    m_Node.m_Id = std::move(id);
    Invalidate(Invalidation::Selector);
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
        Invalidate(Invalidation::Selector);
    }
}

void Widget::RemoveClass(const std::string& name)
{
    VerifyAccess();
    if (std::erase(m_Node.m_Classes, name))
        Invalidate(Invalidation::Selector);
}

void Widget::SetEnabled(bool enabled)
{
    VerifyAccess();
    if (m_Enabled == enabled)
        return;
    m_Enabled = enabled;
    Invalidate(Invalidation::Selector);
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
        Invalidate(Invalidation::Style | Invalidation::InheritedStyle | Invalidation::Content);
    }
}

bool Widget::IsVisible() const
{
    VerifyAccess();
    return m_Visible && m_Style.m_Displayed && (!m_Parent || m_Parent->IsVisible());
}

void Widget::SetWidth(SizeRule rule)
{
    VerifyStructuralMutation();
    rule.Validate();
    m_WidthRule = rule;
    Invalidate(Invalidation::Style | Invalidation::Content);
}

void Widget::SetHeight(SizeRule rule)
{
    VerifyStructuralMutation();
    rule.Validate();
    m_HeightRule = rule;
    Invalidate(Invalidation::Style | Invalidation::Content);
}

void Widget::SetLayoutDirection(Axis axis)
{
    VerifyStructuralMutation();
    if (axis != Axis::Horizontal && axis != Axis::Vertical)
        throw std::invalid_argument("Invalid layout axis.");
    m_Direction = axis;
    Invalidate(Invalidation::Style | Invalidation::Content);
}

void Widget::SetGap(float pixels)
{
    VerifyStructuralMutation();
    if (!std::isfinite(pixels) || pixels < 0 || pixels > 1000000)
        throw std::invalid_argument("A layout gap must be finite and nonnegative.");
    m_Gap = pixels;
    Invalidate(Invalidation::Style | Invalidation::Content);
}

void Widget::SetLayout(std::unique_ptr<LayoutStrategy> strategy)
{
    VerifyStructuralMutation();
    if (!strategy && !m_Layout)
        return;
    m_Layout = std::move(strategy);
    Invalidate(Invalidation::Content);
}

const LayoutStrategy* Widget::GetLayout() const
{
    VerifyAccess();
    return m_Layout.get();
}

void Widget::SetAlignment(LayoutAlignment alignment)
{
    VerifyStructuralMutation();
    if (alignment < LayoutAlignment::Stretch || alignment > LayoutAlignment::Baseline)
        throw std::invalid_argument("Invalid layout alignment.");
    if (m_Alignment == alignment)
        return;
    m_Alignment = alignment;
    Invalidate(Invalidation::Content);
}

void Widget::SetJustification(LayoutJustification justification)
{
    VerifyStructuralMutation();
    if (justification < LayoutJustification::Start || justification > LayoutJustification::SpaceBetween)
        throw std::invalid_argument("Invalid layout justification.");
    if (m_Justification == justification)
        return;
    m_Justification = justification;
    Invalidate(Invalidation::Content);
}

LayoutReport Widget::GetLayoutReport() const
{
    VerifyAccess();
    return m_LayoutReport;
}

void Widget::SetBaselineChild(Widget* child)
{
    VerifyStructuralMutation();
    if (child)
        child->VerifyAccess();
    if (child && (child->m_Parent != this || child->m_FloatingBounds || child->m_Overlay))
        throw std::invalid_argument("A baseline source must be an immediate in-flow child.");
    const auto key = child ? child->m_TargetKey : 0;
    if (m_BaselineChild == key)
        return;
    m_BaselineChild = key;
    Invalidate(Invalidation::Content);
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
    const bool wasFloating = m_FloatingBounds.has_value();
    m_FloatingBounds = bounds;
    Invalidate(wasFloating ? Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint
                           : Invalidation::Content);
}

Widget* Widget::GetParent() const
{
    VerifyAccess();
    return m_Parent;
}

void Widget::ClearFloatingBounds()
{
    VerifyAccess();
    if (!m_FloatingBounds)
        return;
    m_FloatingBounds.reset();
    Invalidate(Invalidation::Content);
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
        RefreshStyleParts();
        Invalidate(GetStylePropertyInvalidation(property));
    }
    else
        Invalidate(Invalidation::Style | GetStylePropertyInvalidation(property));
}

void Widget::RefreshStyleParts()
{
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
}

void Widget::ClearStyleProperty(StyleProperty property)
{
    VerifyAccess();
    if (!m_StyleOverrides.erase(property))
        return;
    if (IsPaintOnlyProperty(property))
    {
        const auto* presentation = m_Context ? m_Context->m_CssMotion->Presentation(*this) : nullptr;
        SetStyleValue(m_Style, property, GetStyleValue(presentation ? *presentation : m_CascadedStyle, property));
        RefreshStyleParts();
        Invalidate(GetStylePropertyInvalidation(property));
    }
    else
        Invalidate(Invalidation::Style | GetStylePropertyInvalidation(property));
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

Point Widget::GetIntrinsicSize() const
{
    VerifyAccess();
    return m_IntrinsicSize;
}

MeasureResult Widget::GetContentMeasurement() const
{
    VerifyAccess();
    return m_ContentMeasurement;
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

void Widget::OnFontsChanged() noexcept
{
}

Point Widget::Measure(FontFace&) const
{
    VerifyAccess();
    return {};
}

MeasureResult Widget::MeasureConstrained(FontFace&, const MeasureConstraints&) const
{
    return {{}, GetIntrinsicSize(), {}, false};
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

bool Widget::OnInput(const InputEvent& event)
{
    VerifyAccess();
    if (const auto* drop = std::get_if<FileDropEvent>(&event))
    {
        if (!m_AcceptsDrops)
            return false;
        m_FilesDropped.Emit(*drop);
        return true;
    }
    if (const auto* key = std::get_if<KeyboardEvent>(&event))
        OnKey(key->m_Key, key->m_Down, key->m_Repeat, EditingModifiers(key->m_Modifiers));
    else if (const auto* text = std::get_if<TextInputEvent>(&event))
        OnText(text->m_Text);
    else if (const auto* pointer = std::get_if<PointerEvent>(&event))
    {
        if (pointer->m_Type == PointerEventType::Cancel)
            OnCaptureLost();
        else if (pointer->m_Type == PointerEventType::Move)
            OnPointerMove(*pointer);
        else if (pointer->m_Button == PointerButton::Left && pointer->m_Type == PointerEventType::Down)
            OnPointerDown(*pointer);
        else if (pointer->m_Button == PointerButton::Left && pointer->m_Type == PointerEventType::Up)
            OnPointerUp(*pointer);
        else
            return false;
    }
    else if (const auto* wheel = std::get_if<ScrollEvent>(&event))
        return OnScroll(wheel->m_Delta, EditingModifiers(wheel->m_Modifiers));
    return true;
}

void Widget::SetAcceptDrops(bool accept)
{
    VerifyStructuralMutation();
    m_AcceptsDrops = accept;
}

bool Widget::AcceptsDrops() const
{
    VerifyAccess();
    return m_AcceptsDrops;
}

Signal<const FileDropEvent&>& Widget::FilesDropped()
{
    VerifyAccess();
    return m_FilesDropped;
}

void Widget::SetCursor(std::optional<CursorShape> shape)
{
    VerifyStructuralMutation();
    if (shape && (*shape < CursorShape::Arrow || *shape > CursorShape::NotAllowed))
        throw std::invalid_argument("Invalid widget cursor shape.");
    if (shape != m_Cursor)
    {
        m_Cursor = shape;
        Invalidate(Invalidation::HitTest);
    }
}

std::optional<CursorShape> Widget::GetCursor() const
{
    VerifyAccess();
    return m_Cursor;
}

bool Widget::OnInputPreview(const InputEvent& event)
{
    VerifyAccess();
    if (const auto* key = std::get_if<KeyboardEvent>(&event))
        return OnKeyPreview(key->m_Key, key->m_Down, key->m_Repeat, EditingModifiers(key->m_Modifiers));
    return false;
}

void Widget::SetFocusScope(bool enabled)
{
    VerifyStructuralMutation();
    m_FocusScope = enabled;
}

bool Widget::IsFocusScope() const
{
    VerifyAccess();
    return m_FocusScope;
}

void Widget::AddAction(std::shared_ptr<Action> action, ShortcutScope scope)
{
    VerifyStructuralMutation();
    if (!action || scope < ShortcutScope::Widget || scope > ShortcutScope::Window)
        throw std::invalid_argument("An action association requires a valid action and scope.");
    (void)action->GetSnapshot();
    RemoveAction(action);
    m_Actions.push_back({action, scope});
    if (m_Context)
        m_Context->RegisterAction(*this, std::move(action), scope);
}

void Widget::RemoveAction(const std::shared_ptr<Action>& action)
{
    VerifyStructuralMutation();
    if (!std::erase_if(m_Actions, [&](const ActionAssociation& association) { return association.m_Action == action; }))
        return;
    if (m_Context)
    {
        m_Context->UnregisterActions(*this);
        for (const auto& association : m_Actions)
            m_Context->RegisterAction(*this, association.m_Action, association.m_Scope);
    }
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

void Widget::Invalidate(Invalidation flags)
{
    VerifyAccess();
    if (m_Context)
        m_Context->Invalidate(*this, flags);
    else
    {
        m_StyleDirty |= HasInvalidation(flags, Invalidation::Style | Invalidation::Selector | Invalidation::Structure);
        m_SubtreeStyleDirty |= m_StyleDirty;
        m_DescendantStyleDirty |= HasInvalidation(flags, Invalidation::InheritedStyle | Invalidation::Structure);
        if (HasInvalidation(flags, Invalidation::Measure | Invalidation::Structure))
            m_IntrinsicValid = m_MeasureValid = false;
        m_ArrangeDirty |=
            HasInvalidation(flags, Invalidation::Measure | Invalidation::Arrange | Invalidation::Structure);
    }
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
    Invalidate(Invalidation::Style | Invalidation::Content);
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
    Invalidate(Invalidation::Style | Invalidation::Content);
}

void Widget::SetState(unsigned flag, bool enabled)
{
    VerifyAccess();
    const auto state = enabled ? m_Node.m_State | flag : m_Node.m_State & ~flag;
    if (state != m_Node.m_State)
    {
        m_Node.m_State = state;
        Invalidate(Invalidation::Selector);
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

namespace EStudioUI
{
void Widget::SetAccessibleName(std::optional<std::string> name)
{
    VerifyStructuralMutation();
    if (name)
        ValidatePlatformText(*name);
    m_AccessibleName = std::move(name);
    NotifySemanticsChanged();
}

void Widget::SetAccessibleDescription(std::optional<std::string> description)
{
    VerifyStructuralMutation();
    if (description)
        ValidatePlatformText(*description);
    m_AccessibleDescription = std::move(description);
    NotifySemanticsChanged();
}

void Widget::SetAccessibleRole(std::optional<SemanticRole> role)
{
    VerifyStructuralMutation();
    if (role && (*role < SemanticRole::Group || *role > SemanticRole::CanvasItem))
        throw std::invalid_argument("Invalid accessible role.");
    m_AccessibleRole = role;
    NotifySemanticsChanged();
}

void Widget::NotifySemanticsChanged()
{
    VerifyStructuralMutation();
    if (m_Context && m_Context->m_Semantics)
    {
        m_Context->m_Semantics->Changed();
        m_Context->m_Dispatcher.PostLatest("semantic-wake:" + std::to_string(m_Context->m_Root.m_TargetKey), [] {});
    }
}
} // namespace EStudioUI
