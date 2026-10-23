#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/ClipboardData.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <limits>

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
    ++m_StructureRevision;
    m_Widgets.insert_or_assign(widget.m_TargetKey, WidgetRef(&widget));
}

void SemanticTree::Unregister(const Widget& widget)
{
    Changed();
    ++m_StructureRevision;
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
    if (!id.m_Key.m_Item && id.m_Key != SemanticKey{})
        return {};
    const auto found = m_Widgets.find(id.m_Widget);
    if (found == m_Widgets.end())
        return {};
    const auto* widget = found->second.Get();
    if (!widget || widget->m_Context != &m_Context)
        return {};
    const QueryScope scope(m_Context);
    const bool isVirtual = id.m_Key.m_Item != 0;
    const auto child = isVirtual ? widget->DescribeSemanticChild(id.m_Key) : std::optional<SemanticVirtualNode>{};
    if (isVirtual && !child)
        return {};
    if (child)
    {
        const auto& bounds = child->m_Bounds;
        if (!std::isfinite(bounds.m_X) || !std::isfinite(bounds.m_Y) || !std::isfinite(bounds.m_Width) ||
            !std::isfinite(bounds.m_Height) || bounds.m_Width < 0 || bounds.m_Height < 0 ||
            !std::isfinite(bounds.m_X + bounds.m_Width) || !std::isfinite(bounds.m_Y + bounds.m_Height))
            throw std::invalid_argument("A semantic child returned invalid finite nonnegative geometry.");
        if ((!child->m_Parent.m_Item && child->m_Parent != SemanticKey{}) || child->m_Parent == id.m_Key ||
            (child->m_Parent.m_Item && !widget->DescribeSemanticChild(child->m_Parent)))
            throw std::invalid_argument("A semantic child returned an invalid logical parent.");
    }
    SemanticNode node;
    node.m_Id = id;
    if (child)
        node.m_Parent = SemanticId{id.m_Widget, child->m_Parent};
    else if (const auto* parent = m_Context.LogicalParent(*widget))
        node.m_Parent = parent->m_TargetKey;
    node.m_Info = child ? child->m_Info : widget->DescribeSemantics();
    node.m_TreeStructureRevision = m_StructureRevision;
    if (!child && widget == &m_Context.m_Root)
        node.m_Info.m_Role = SemanticRole::Window;
    if (!child && widget->m_AccessibleName)
        node.m_Info.m_Name = *widget->m_AccessibleName;
    if (!child && widget->m_AccessibleDescription)
        node.m_Info.m_Description = *widget->m_AccessibleDescription;
    if (!child && widget->m_AccessibleRole)
        node.m_Info.m_Role = *widget->m_AccessibleRole;
    auto& info = node.m_Info;
    if (info.m_Role < SemanticRole::Group || info.m_Role > SemanticRole::ToolTip ||
        (static_cast<unsigned>(info.m_Actions) & ~2047U))
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
    node.m_Enabled = widget->IsEnabled() && node.m_Visible && IsAllowed(*widget) && (!child || child->m_Enabled);
    node.m_Focusable = child ? child->m_Focusable : widget->IsFocusable();
    node.m_Focused = m_Context.m_Focus.Get() == widget && (!child || child->m_Focused);
    if (child && HasSemanticAction(info.m_Actions, SemanticAction::Select))
        node.m_SelectionContainer = id.m_Widget;
    if (child && info.m_Role == SemanticRole::Cell)
        node.m_GridContainer = id.m_Widget;
    if (!child && info.m_Role == SemanticRole::Tab)
        for (const auto* ancestor = m_Context.LogicalParent(*widget); ancestor;
             ancestor = m_Context.LogicalParent(*ancestor))
            if (ancestor->DescribeSemantics().m_Role == SemanticRole::TabList)
            {
                node.m_SelectionContainer = ancestor->m_TargetKey;
                break;
            }
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
    node.m_Bounds = transformed(widget, child ? child->m_Bounds : widget->m_Bounds);
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

WidgetRef SemanticTree::GetWidget(SemanticId id) const
{
    VerifyQuery();
    if (id.m_Key != SemanticKey{})
        return {};
    const auto found = m_Widgets.find(id.m_Widget);
    return found == m_Widgets.end() ? WidgetRef{} : found->second;
}

std::size_t SemanticTree::GetChildCount(SemanticId id) const
{
    VerifyQuery();
    if (!id.m_Key.m_Item && id.m_Key != SemanticKey{})
        return 0;
    const auto found = m_Widgets.find(id.m_Widget);
    const auto* parent = found == m_Widgets.end() ? nullptr : found->second.Get();
    if (!parent)
        return 0;
    const QueryScope scope(m_Context);
    if (id.m_Key.m_Item && !parent->DescribeSemanticChild(id.m_Key))
        return 0;
    auto count = parent->GetSemanticChildCount(id.m_Key);
    if (id.m_Key.m_Item)
        return count;
    for (const auto& child : parent->m_Children)
        if (!child->m_Overlay && m_Context.LogicalParent(*child) == parent && IsVisible(*child))
            ++count;
    for (const auto& popup : m_Context.m_Popups)
        if (const auto* child = popup.m_Widget.Get();
            child && m_Context.LogicalParent(*child) == parent && IsVisible(*child))
            ++count;
    return count;
}

SemanticId SemanticTree::GetChildAt(SemanticId id, std::size_t index) const
{
    VerifyQuery();
    if (!id.m_Key.m_Item && id.m_Key != SemanticKey{})
        return 0;
    const auto found = m_Widgets.find(id.m_Widget);
    const auto* parent = found == m_Widgets.end() ? nullptr : found->second.Get();
    if (!parent)
        return 0;
    const QueryScope scope(m_Context);
    if (id.m_Key.m_Item)
    {
        if (!parent->DescribeSemanticChild(id.m_Key))
            return 0;
        const auto key = parent->GetSemanticChildAt(id.m_Key, index);
        if (key && !key->m_Item)
            throw std::invalid_argument("A virtual semantic child must identify a nonzero stable item.");
        return key ? SemanticId{id.m_Widget, *key} : SemanticId{};
    }
    for (const auto& child : parent->m_Children)
        if (!child->m_Overlay && m_Context.LogicalParent(*child) == parent && IsVisible(*child))
        {
            if (!index)
                return child->m_TargetKey;
            --index;
        }
    for (const auto& popup : m_Context.m_Popups)
        if (const auto* child = popup.m_Widget.Get();
            child && m_Context.LogicalParent(*child) == parent && IsVisible(*child))
        {
            if (!index)
                return child->m_TargetKey;
            --index;
        }
    const auto key = parent->GetSemanticChildAt({}, index);
    if (key && !key->m_Item)
        throw std::invalid_argument("A virtual semantic child must identify a nonzero stable item.");
    return key ? SemanticId{id.m_Widget, *key} : SemanticId{};
}

std::vector<SemanticId> SemanticTree::GetChildren(SemanticId id, std::size_t offset, std::size_t limit) const
{
    VerifyQuery();
    if (limit > 256)
        throw std::length_error("A semantic child page is limited to 256 identities.");
    std::vector<SemanticId> result;
    for (std::size_t i = 0; i < limit && offset <= std::numeric_limits<std::size_t>::max() - i; ++i)
    {
        const auto child = GetChildAt(id, offset + i);
        if (!child)
            break;
        result.push_back(child);
    }
    return result;
}

SemanticId SemanticTree::GetSibling(SemanticId id, bool next) const
{
    const auto node = GetNode(id);
    if (!node || !node->m_Parent)
        return 0;
    if (id.m_Key.m_Item)
    {
        const auto* owner = m_Widgets.at(id.m_Widget).Get();
        std::optional<std::size_t> index;
        {
            const QueryScope scope(m_Context);
            index = owner->GetSemanticChildIndex(id.m_Key);
        }
        if (!index)
            return 0;
        // Actual owned widgets precede virtual records only at the containing root.
        std::size_t prefix = 0;
        if (!node->m_Parent.m_Key.m_Item)
        {
            const QueryScope scope(m_Context);
            for (const auto& child : owner->m_Children)
                if (!child->m_Overlay && m_Context.LogicalParent(*child) == owner && IsVisible(*child))
                    ++prefix;
            for (const auto& popup : m_Context.m_Popups)
                if (const auto* child = popup.m_Widget.Get();
                    child && m_Context.LogicalParent(*child) == owner && IsVisible(*child))
                    ++prefix;
        }
        if (!next && !prefix && !*index)
            return 0;
        return GetChildAt(node->m_Parent, next ? prefix + *index + 1 : prefix + *index - 1);
    }
    const auto count = GetChildCount(node->m_Parent);
    for (std::size_t index = 0; index < count; ++index)
        if (GetChildAt(node->m_Parent, index) == id)
            return (!next && !index) ? SemanticId{} : GetChildAt(node->m_Parent, index + (next ? 1 : -1));
    return 0;
}

std::size_t SemanticTree::GetSelectionCount(SemanticId id) const
{
    const auto widget = GetWidget(id);
    if (!widget.Get())
        return 0;
    const QueryScope scope(m_Context);
    return widget.Get()->GetSemanticSelectionCount();
}

std::vector<SemanticId> SemanticTree::GetSelection(SemanticId id, std::size_t offset, std::size_t limit) const
{
    if (limit > 256)
        throw std::length_error("A semantic selection page is limited to 256 identities.");
    const auto widget = GetWidget(id);
    if (!widget.Get())
        return {};
    const QueryScope scope(m_Context);
    const auto actual = widget.Get()->GetSemanticSelectedWidgets(offset, limit);
    if (!actual.empty())
    {
        if (actual.size() > limit)
            throw std::length_error("A semantic actual-selection hook exceeded its requested page.");
        std::vector<SemanticId> result;
        for (const auto& child : actual)
            if (const auto* target = child.Get(); target && target->m_Context == &m_Context)
                result.emplace_back(target->m_TargetKey);
        return result;
    }
    const auto keys = widget.Get()->GetSemanticSelection(offset, limit);
    if (keys.size() > limit)
        throw std::length_error("A semantic selection hook exceeded its requested page.");
    std::vector<SemanticId> result;
    for (const auto key : keys)
        result.emplace_back(id.m_Widget, key);
    return result;
}

SemanticId SemanticTree::GetGridItem(SemanticId id, std::size_t row, std::size_t column) const
{
    const auto widget = GetWidget(id);
    if (!widget.Get())
        return 0;
    const QueryScope scope(m_Context);
    const auto key = widget.Get()->GetSemanticGridItem(row, column);
    return key ? SemanticId{id.m_Widget, *key} : SemanticId{};
}

SemanticId SemanticTree::GetFocus() const
{
    VerifyQuery();
    const auto* focused = m_Context.m_Focus.Get();
    if (!focused)
        return 0;
    const QueryScope scope(m_Context);
    const auto key = focused->GetSemanticFocus();
    return key ? SemanticId{focused->m_TargetKey, *key} : SemanticId{focused->m_TargetKey};
}

SemanticId SemanticTree::HitTest(Point windowPoint) const
{
    VerifyQuery();
    if (!std::isfinite(windowPoint.m_X) || !std::isfinite(windowPoint.m_Y))
        throw std::invalid_argument("Semantic hit testing requires finite coordinates.");
    const auto* widget = m_Context.InputHitTest(windowPoint);
    if (!widget)
        return 0;
    const QueryScope scope(m_Context);
    const auto key = widget->SemanticHitTest(m_Context.ToLayoutPoint(*widget, windowPoint));
    return key ? SemanticId{widget->m_TargetKey, *key} : SemanticId{widget->m_TargetKey};
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
    auto* widget = m_Widgets.at(id.m_Widget).Get();
    if (command.m_Action == SemanticAction::Focus)
    {
        // Native and application focus callbacks may replace a dataset while retaining its view.
        // Finish input delivery before querying again: semantic inspection never nests input.
        const WidgetRef reference(widget);
        {
            const Detail::InputDispatchScope inputScope;
            m_Context.Platform().FocusWindow();
            widget = reference.Get();
            if (!widget || m_Context.m_Dispatcher.IsClosed())
                return SemanticStatus::Unavailable;
            m_Context.SetFocus(widget);
        }
        if (!reference.Get() || m_Context.m_Dispatcher.IsClosed())
            return SemanticStatus::Unavailable;
        const auto current = GetNode(id);
        if (!current)
            return SemanticStatus::Unavailable;
        if (!current->m_Enabled)
            return SemanticStatus::Disabled;
        if (!HasSemanticAction(current->m_Info.m_Actions, SemanticAction::Focus))
            return SemanticStatus::Unsupported;
        const Detail::InputDispatchScope inputScope;
        return id.m_Key.m_Item ? reference.Get()->OnSemanticChildAction(id.m_Key, command) : SemanticStatus::Succeeded;
    }
    const Detail::InputDispatchScope inputScope;
    return id.m_Key.m_Item ? widget->OnSemanticChildAction(id.m_Key, command) : widget->OnSemanticAction(command);
}
} // namespace EStudioUI
