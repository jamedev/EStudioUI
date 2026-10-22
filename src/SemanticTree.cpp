#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/ClipboardData.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
struct SemanticTree::QueryScope
{
    UiContext& m_Context;

    explicit QueryScope(UiContext& context) : m_Context(context)
    {
        m_Context.m_Phase = UiPhase::Inspection;
    }

    ~QueryScope()
    {
        m_Context.m_Phase = UiPhase::Idle;
    }
};

SemanticTree::SemanticTree(UiContext& context) : m_Context(context)
{
    const auto visit = [&](auto&& self, Widget& widget) -> void
    {
        Register(widget);
        for (const auto& child : widget.m_Children)
            self(self, *child);
    };
    visit(visit, context.m_Root);
}

SemanticTree::~SemanticTree() = default;

void SemanticTree::Register(Widget& widget)
{
    Changed();
    m_Widgets.insert_or_assign(widget.m_TargetKey, WidgetRef(&widget));
}

void SemanticTree::Unregister(const Widget& widget)
{
    Changed();
    m_Widgets.erase(widget.m_TargetKey);
}

std::uint64_t SemanticTree::GetRevision() const
{
    m_Context.VerifyAccess();
    return m_Revision;
}

void SemanticTree::Changed()
{
    ++m_Revision;
}

void SemanticTree::VerifyQuery() const
{
    m_Context.VerifyTraversalEntry();
}

SemanticId SemanticTree::GetRoot() const
{
    VerifyQuery();
    return m_Context.m_Root.m_TargetKey;
}

SemanticId SemanticTree::GetId(const Widget& widget) const
{
    VerifyQuery();
    widget.VerifyAccess();
    return widget.m_Context == &m_Context ? widget.m_TargetKey : 0;
}

bool SemanticTree::IsVisible(const Widget& widget) const
{
    if (!widget.IsVisible())
        return false;
    for (auto* node = &widget; node; node = node->m_Parent)
        if (node->m_Overlay)
            return std::any_of(m_Context.m_Popups.begin(), m_Context.m_Popups.end(),
                               [&](const auto& popup) { return popup.m_Widget.Get() == node; });
    return true;
}

bool SemanticTree::IsAllowed(const Widget& widget) const
{
    for (auto entry = m_Context.m_Popups.rbegin(); entry != m_Context.m_Popups.rend(); ++entry)
    {
        if (m_Context.IsWithin(&widget, entry->m_Widget.Get()))
            return true;
        if (entry->m_Options.m_Modal)
            return false;
    }
    return true;
}

std::optional<SemanticNode> SemanticTree::GetNode(SemanticId id) const
{
    VerifyQuery();
    const auto found = m_Widgets.find(id);
    if (found == m_Widgets.end())
        return {};
    const auto* widget = found->second.Get();
    if (!widget || widget->m_Context != &m_Context)
        return {};
    const QueryScope scope(m_Context);
    SemanticNode node;
    node.m_Id = id;
    if (const auto* parent = m_Context.LogicalParent(*widget))
        node.m_Parent = parent->m_TargetKey;
    node.m_Info = widget->DescribeSemantics();
    if (widget == &m_Context.m_Root)
        node.m_Info.m_Role = SemanticRole::Window;
    if (widget->m_AccessibleName)
        node.m_Info.m_Name = *widget->m_AccessibleName;
    if (widget->m_AccessibleDescription)
        node.m_Info.m_Description = *widget->m_AccessibleDescription;
    if (widget->m_AccessibleRole)
        node.m_Info.m_Role = *widget->m_AccessibleRole;
    auto& info = node.m_Info;
    if (info.m_Role < SemanticRole::Group || info.m_Role > SemanticRole::Canvas ||
        (static_cast<unsigned>(info.m_Actions) & ~31U))
        throw std::invalid_argument("A semantic hook returned an invalid role or action mask.");
    for (const auto* text : {&info.m_Name, &info.m_Description, &info.m_Value})
        ValidatePlatformText(*text);
    for (const auto value : {info.m_Number, info.m_Minimum, info.m_Maximum, info.m_Step})
        if (value && !std::isfinite(*value))
            throw std::invalid_argument("Semantic numeric metadata must be finite.");
    if ((info.m_Minimum && info.m_Maximum && *info.m_Minimum > *info.m_Maximum) || (info.m_Step && *info.m_Step <= 0))
        throw std::invalid_argument("Semantic numeric metadata has an invalid range or step.");
    node.m_ClassName = widget->m_Node.m_Type;
    node.m_AutomationId = widget->m_Node.m_Id;
    node.m_Visible = IsVisible(*widget);
    node.m_Enabled = widget->IsEnabled() && node.m_Visible && IsAllowed(*widget);
    node.m_Focusable = widget->IsFocusable();
    node.m_Focused = m_Context.m_Focus.Get() == widget;
    if (node.m_Focusable)
        info.m_Actions = info.m_Actions | SemanticAction::Focus;
    const auto transformed = [&](const Widget* target, Rect rect)
    {
        Point points[4] = {{rect.m_X, rect.m_Y},
                           {(rect.m_X + rect.m_Width), rect.m_Y},
                           {rect.m_X, (rect.m_Y + rect.m_Height)},
                           {(rect.m_X + rect.m_Width), (rect.m_Y + rect.m_Height)}};
        if (target)
            for (auto& point : points)
                point = m_Context.ToWindowPoint(*target, point);
        float left = points[0].m_X, top = points[0].m_Y, right = left, bottom = top;
        for (const auto point : points)
        {
            left = std::min(left, point.m_X);
            top = std::min(top, point.m_Y);
            right = std::max(right, point.m_X);
            bottom = std::max(bottom, point.m_Y);
        }
        return Rect{left, top, right - left, bottom - top};
    };
    node.m_Bounds = transformed(widget, widget->m_Bounds);
    for (auto* ancestor = widget; ancestor; ancestor = ancestor->m_Parent)
    {
        node.m_Bounds =
            node.m_Bounds.Intersect(transformed(ancestor->m_Overlay ? nullptr : ancestor->m_Parent, ancestor->m_Clip));
        if (ancestor != widget && ancestor->m_Style.m_ClipContent)
            node.m_Bounds = node.m_Bounds.Intersect(transformed(ancestor, ancestor->m_Bounds));
        if (ancestor->m_Overlay)
            break;
    }
    node.m_Offscreen = !node.m_Visible || node.m_Bounds.m_Width <= 0 || node.m_Bounds.m_Height <= 0;
    // Native coordinate metrics are pure owner-side reads, independent of layout and paint.
    const auto metrics = m_Context.Platform().GetWindowMetrics();
    if (metrics.m_HasScreenCoordinates)
        node.m_ScreenBounds = metrics.ToScreen(node.m_Bounds);
    return node;
}

std::vector<SemanticId> SemanticTree::GetChildren(SemanticId id, std::size_t offset, std::size_t limit) const
{
    VerifyQuery();
    if (limit > 256)
        throw std::length_error("A semantic child page is limited to 256 identities.");
    std::vector<SemanticId> result;
    const auto found = m_Widgets.find(id);
    if (found == m_Widgets.end() || !limit)
        return result;
    const auto* parent = found->second.Get();
    if (!parent)
        return result;
    const auto append = [&](const Widget& widget)
    {
        if (m_Context.LogicalParent(widget) != parent || !IsVisible(widget))
            return;
        if (offset)
        {
            --offset;
            return;
        }
        if (result.size() < limit)
            result.push_back(widget.m_TargetKey);
    };
    for (const auto& child : parent->m_Children)
    {
        if (!child->m_Overlay)
            append(*child);
        if (result.size() == limit)
            return result;
    }
    for (const auto& popup : m_Context.m_Popups)
        if (const auto* widget = popup.m_Widget.Get())
            append(*widget);
    return result;
}

SemanticId SemanticTree::GetFocus() const
{
    VerifyQuery();
    const auto* focused = m_Context.m_Focus.Get();
    return focused ? focused->m_TargetKey : 0;
}

SemanticId SemanticTree::HitTest(Point windowPoint) const
{
    VerifyQuery();
    if (!std::isfinite(windowPoint.m_X) || !std::isfinite(windowPoint.m_Y))
        throw std::invalid_argument("Semantic hit testing requires finite coordinates.");
    const auto* widget = m_Context.InputHitTest(windowPoint);
    return widget ? widget->m_TargetKey : 0;
}

SemanticStatus SemanticTree::Perform(SemanticId id, const SemanticCommand& command)
{
    const auto ownerLease = m_Context.RetainOwner();
    const auto node = GetNode(id);
    if (!node)
        return SemanticStatus::Unavailable;
    if (!node->m_Enabled)
        return SemanticStatus::Disabled;
    if (!std::has_single_bit(static_cast<unsigned>(command.m_Action)) ||
        !HasSemanticAction(node->m_Info.m_Actions, command.m_Action))
        return SemanticStatus::Unsupported;
    try
    {
        ValidatePlatformText(command.m_Text);
    }
    catch (const std::logic_error&)
    {
        return SemanticStatus::InvalidValue;
    }
    if (!std::isfinite(command.m_Number))
        return SemanticStatus::InvalidValue;
    auto* widget = m_Widgets.at(id).Get();
    const Detail::InputDispatchScope inputScope;
    if (command.m_Action == SemanticAction::Focus)
    {
        // Native focus may synchronously notify application code; recheck the weak target afterwards.
        const WidgetRef reference(widget);
        m_Context.Platform().FocusWindow();
        widget = reference.Get();
        if (!widget || m_Context.m_Dispatcher.IsClosed())
            return SemanticStatus::Unavailable;
        m_Context.SetFocus(widget);
        return SemanticStatus::Succeeded;
    }
    return widget->OnSemanticAction(command);
}
} // namespace EStudioUI
