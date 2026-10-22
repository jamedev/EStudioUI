#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace EStudioUI
{
namespace
{
struct FlagScope
{
    bool& m_Flag;
    bool m_Previous;

    explicit FlagScope(bool& flag) : m_Flag(flag), m_Previous(std::exchange(flag, true))
    {
    }

    ~FlagScope()
    {
        m_Flag = m_Previous;
    }
};
} // namespace

std::shared_ptr<Action> UiContext::CreateAction(std::string text)
{
    VerifyDispatchPhase();
    return std::make_shared<Action>(States(), std::move(text));
}

ShortcutStatus UiContext::GetLastShortcutStatus() const
{
    VerifyAccess();
    return m_LastShortcut;
}

void UiContext::RegisterAction(Widget& owner, const std::shared_ptr<Action>& action, ShortcutScope scope)
{
    m_ActionAssociations.push_back({WidgetRef(&owner), action, scope});
}

void UiContext::UnregisterActions(const Widget& owner)
{
    std::erase_if(m_ActionAssociations, [&](const ActionAssociation& entry)
                  { return !entry.m_Owner.Get() || entry.m_Owner.Get() == &owner; });
}

Widget* UiContext::LogicalParent(const Widget& widget) const
{
    if (widget.m_Overlay)
    {
        for (const auto& owned : m_OwnedPopups)
            if (owned.m_Popup.Get() == &widget)
                if (auto* owner = owned.m_Owner.Get(); owner && owner->m_Context == this)
                    return owner;
        for (const auto& entry : m_Popups)
            if (entry.m_Widget.Get() == &widget)
                if (auto* parent = entry.m_Parent.Get())
                    return parent;
    }
    return widget.m_Parent;
}

bool UiContext::IsWithin(const Widget* widget, const Widget* ancestor) const
{
    if (!ancestor)
        return false;
    for (auto* node = widget; node; node = LogicalParent(*node))
        if (node == ancestor)
            return true;
    return false;
}

Widget& UiContext::InputRoot() const
{
    for (auto i = m_Popups.rbegin(); i != m_Popups.rend(); ++i)
        if (auto* popup = i->m_Widget.Get())
            return *popup;
    return const_cast<Widget&>(m_Root);
}

Widget* UiContext::InputHitTest(Point position) const
{
    for (auto i = m_Popups.rbegin(); i != m_Popups.rend(); ++i)
    {
        if (auto* popup = i->m_Widget.Get())
            if (auto* target = HitTest(*popup, position))
                return target;
        if (i->m_Options.m_Modal)
            return nullptr;
    }
    return HitTest(const_cast<Widget&>(m_Root), position);
}

void UiContext::PrepareInput()
{
    Prepare(m_Width, m_Height);
}

bool UiContext::DispatchInput(const InputEvent& event)
{
    const auto ownerLifetime = RetainOwner();
    VerifyTraversalEntry();
    if (m_DispatchingInput)
        throw std::logic_error("Nested input delivery is unsupported; post the next event instead.");
    ValidateInput(event);
    const InputEvent input = event;
    const Detail::InputDispatchScope inputScope;
    const FlagScope scope(m_DispatchingInput);
    PrepareInput();
    if (const auto* key = std::get_if<KeyboardEvent>(&input))
        return RouteKey(*key);
    if (const auto* pointer = std::get_if<PointerEvent>(&input))
        return RoutePointer(*pointer);
    if (std::holds_alternative<TextInputEvent>(input))
    {
        const auto target = m_Focus;
        if (auto* focused = target.Get(); focused && focused->IsEnabled())
        {
            const auto revision = m_PopupRevision;
            if (PreviewInput(*focused, input))
                return true;
            if (target.Get() && m_Focus.Get() == target.Get() && revision == m_PopupRevision)
                return target.Get()->OnInput(input);
        }
        return false;
    }
    const auto* wheel = std::get_if<ScrollEvent>(&input);
    const auto* drop = std::get_if<FileDropEvent>(&input);
    const auto position = wheel ? wheel->m_Position : drop->m_Position;
    m_PointerPosition = position;
    auto* target = InputHitTest(position);
    if (!target)
        return false;
    const WidgetRef reference(target);
    if (PreviewInput(*target, input))
        return true;
    std::vector<WidgetRef> path;
    const Widget* boundary = &m_Root;
    for (auto entry = m_Popups.rbegin(); entry != m_Popups.rend(); ++entry)
        if (IsWithin(reference.Get(), entry->m_Widget.Get()))
        {
            boundary = entry->m_Widget.Get();
            break;
        }
    for (auto* node = reference.Get(); node; node = LogicalParent(*node))
    {
        path.emplace_back(node);
        if (node == boundary)
            break;
    }
    const auto bubbleRevision = m_PopupRevision;
    for (const auto& entry : path)
        if (auto* node = entry.Get(); node && node->IsVisible() && node->IsEnabled())
        {
            auto local = input;
            const auto point = ToLayoutPoint(*node, position);
            if (auto* localWheel = std::get_if<ScrollEvent>(&local))
                localWheel->m_Position = point;
            if (auto* localDrop = std::get_if<FileDropEvent>(&local))
                localDrop->m_Position = point;
            if (node->OnInput(local) || bubbleRevision != m_PopupRevision)
                return true;
        }
    return false;
}

bool UiContext::PreviewInput(Widget& target, const InputEvent& event)
{
    std::vector<WidgetRef> path;
    const Widget* boundary = &m_Root;
    for (auto entry = m_Popups.rbegin(); entry != m_Popups.rend(); ++entry)
        if (IsWithin(&target, entry->m_Widget.Get()))
        {
            boundary = entry->m_Widget.Get();
            break;
        }
    const auto revision = m_PopupRevision;
    for (auto* node = &target; node; node = LogicalParent(*node))
    {
        path.emplace_back(node);
        if (node == boundary)
            break;
    }
    for (const auto& reference : path)
    {
        auto* node = reference.Get();
        if (!node || !node->IsEnabled() || !node->IsVisible())
            continue;
        auto local = event;
        if (auto* pointer = std::get_if<PointerEvent>(&local))
        {
            const auto mapped = MakePointerEvent(*node, pointer->m_Position, pointer->m_Modifiers, pointer->m_Clicks);
            pointer->m_Position = mapped.m_Position;
            pointer->m_InsideClip = mapped.m_InsideClip;
        }
        if (auto* wheel = std::get_if<ScrollEvent>(&local))
            wheel->m_Position = ToLayoutPoint(*node, wheel->m_Position);
        if (auto* drop = std::get_if<FileDropEvent>(&local))
            drop->m_Position = ToLayoutPoint(*node, drop->m_Position);
        if (node->OnInputPreview(local) || revision != m_PopupRevision)
            return true;
    }
    return false;
}

bool UiContext::DispatchShortcut(const KeyboardEvent& event)
{
    if (!event.m_Down || event.m_Modifiers.m_AltGraph)
        return false;
    std::shared_ptr<Action> selected;
    int best = std::numeric_limits<int>::min();
    bool ambiguous = false;
    Widget* modal = nullptr;
    for (auto i = m_Popups.rbegin(); i != m_Popups.rend(); ++i)
        if (i->m_Options.m_Modal)
        {
            modal = i->m_Widget.Get();
            break;
        }
    for (const auto& entry : m_ActionAssociations)
    {
        ++m_Stats.m_ShortcutChecks;
        auto* owner = entry.m_Owner.Get();
        const auto action = entry.m_Action.lock();
        if (!owner || owner->m_Context != this || !owner->IsVisible() || !owner->IsEnabled() || !action ||
            (modal && !IsWithin(owner, modal)))
            continue;
        int rank = 0;
        if (entry.m_Scope == ShortcutScope::Widget)
        {
            if (owner != m_Focus.Get())
                continue;
            rank = 200000;
        }
        else if (entry.m_Scope == ShortcutScope::Subtree)
        {
            int distance = 0;
            auto* node = m_Focus.Get();
            for (; node && node != owner; node = LogicalParent(*node))
                ++distance;
            if (!node)
                continue;
            rank = 100000 - distance;
        }
        if (action->GetStopToken().stop_requested())
            continue;
        const auto state = action->GetSnapshot();
        if (!state->m_Enabled || !state->m_Visible ||
            std::none_of(state->m_Shortcuts.begin(), state->m_Shortcuts.end(),
                         [&](const Shortcut& shortcut) { return shortcut.Matches(event); }))
            continue;
        if (rank > best)
        {
            selected = action;
            best = rank;
            ambiguous = false;
        }
        else if (rank == best && selected != action)
            ambiguous = true;
    }
    if (!selected)
        return false;
    if (ambiguous)
    {
        m_LastShortcut = ShortcutStatus::Ambiguous;
        return true;
    }
    if (!m_Popups.empty() && !m_Popups.back().m_Options.m_Modal)
    {
        std::size_t first = m_Popups.size() - 1;
        while (first && !m_Popups[first - 1].m_Options.m_Modal)
            --first;
        ClosePopupBranch(first, {PopupCloseReason::Closed, 0});
    }
    m_LastShortcut = selected->Trigger() ? ShortcutStatus::Triggered : ShortcutStatus::Unmatched;
    return m_LastShortcut == ShortcutStatus::Triggered;
}

bool UiContext::RouteKey(const KeyboardEvent& event)
{
    m_LastShortcut = ShortcutStatus::Unmatched;
    if (m_CompositionClient.Get() && m_CompositionClient.Get() == m_Focus.Get())
    {
        if (event.m_Down && event.m_Key == Key::Escape)
            CancelTextComposition();
        return true;
    }
    const auto modifiers = EditingModifiers(event.m_Modifiers);
    if (event.m_Down && event.m_Key == Key::Escape && !m_Popups.empty() &&
        m_Popups.back().m_Options.m_DismissOnEscape && !event.m_Modifiers.m_AltGraph)
    {
        ClosePopup({PopupCloseReason::Escape, 0});
        return true;
    }
    if (event.m_Down && (event.m_Key == Key::Menu || (event.m_Key == Key::F10 && modifiers.m_Shift)) &&
        !modifiers.m_Control && !modifiers.m_Alt && m_Focus.Get())
    {
        const auto bounds = m_Focus.Get()->GetBounds();
        ContextMenu(
            ToWindowPoint(*m_Focus.Get(), {bounds.m_X + bounds.m_Width * 0.5f, bounds.m_Y + bounds.m_Height * 0.5f}));
        return true;
    }
    const auto target = m_Focus;
    const bool hadFocus = target.Get() != nullptr;
    const auto revision = m_PopupRevision;
    if (auto* focused = target.Get(); focused && PreviewInput(*focused, event))
        return true;
    if ((hadFocus && !target.Get()) || target.Get() != m_Focus.Get() || revision != m_PopupRevision)
        return true;
    if (DispatchShortcut(event))
        return true;
    if (event.m_Key == Key::Tab && event.m_Down && !event.m_Modifiers.m_AltGraph)
    {
        if (!event.m_Repeat)
            FocusNext(modifiers.m_Shift);
        return true;
    }
    if (auto* focused = m_Focus.Get(); focused && focused->IsEnabled())
        return focused->OnInput(event);
    return false;
}

void UiContext::KeyEvent(Key key, bool down, bool repeat, Modifiers modifiers)
{
    DispatchInput(KeyboardEvent{key, down, repeat, modifiers, key, 0, GetTime()});
}

void UiContext::TextInput(std::string_view text)
{
    DispatchInput(TextInputEvent{std::string(text), GetTime()});
}

bool UiContext::DispatchComposition(const TextCompositionEvent& event)
{
    const auto ownerLifetime = RetainOwner();
    VerifyTraversalEntry();
    if (m_DispatchingInput)
        throw std::logic_error("Nested text composition delivery is unsupported; post the update instead.");
    ValidateTextComposition(event);
    const auto state = GetTextInputState();
    if (!state.m_Client || (event.m_Client && event.m_Client != state.m_Client) ||
        (event.m_Epoch && event.m_Epoch != state.m_Epoch) ||
        (!state.m_Active && event.m_Kind != TextCompositionKind::Cancel))
        return false;
    const auto target = m_Focus;
    const Detail::InputDispatchScope inputScope;
    const FlagScope scope(m_DispatchingInput);
    PrepareInput();
    if (!target.Get() || target.Get() != m_Focus.Get() || m_TextInputEpoch != state.m_Epoch)
        return false;
    auto* client = dynamic_cast<TextInputClient*>(target.Get());
    if (!client)
        return false;
    auto owned = event;
    owned.m_Client = state.m_Client;
    owned.m_Epoch = state.m_Epoch;
    if (event.m_Kind != TextCompositionKind::Preedit)
        m_CompositionClient = {};
    const bool accepted = client->OnTextComposition(owned);
    if (target.Get() && target.Get() == m_Focus.Get())
        ++m_TextInputRevision;
    if (accepted && event.m_Kind == TextCompositionKind::Preedit && target.Get() && target.Get() == m_Focus.Get() &&
        m_TextInputEpoch == state.m_Epoch)
        m_CompositionClient = target;
    return accepted;
}

void UiContext::CancelTextComposition()
{
    const auto ownerLifetime = RetainOwner();
    VerifyDispatchPhase();
    ++m_TextInputEpoch;
    ++m_TextInputRevision;
    m_CompositionClient = {};
    if (m_CancellingComposition)
        return;
    const FlagScope cancelling(m_CancellingComposition);
    const auto target = m_Focus;
    if (auto* client = target.Get() ? dynamic_cast<TextInputClient*>(target.Get()) : nullptr)
        client->OnTextComposition({TextCompositionKind::Cancel, {}, 0, target.Get()->m_TargetKey, m_TextInputEpoch});
}

PointerEvent UiContext::MakePointerEvent(const Widget& widget, Point position, Modifiers modifiers, int clicks) const
{
    std::vector<const Widget*> path;
    for (auto* node = &widget; node; node = node->m_Parent)
    {
        path.push_back(node);
        if (node->m_Overlay)
            break;
    }
    Affine matrix, inverse;
    bool inside = true;
    for (auto i = path.rbegin(); i != path.rend(); ++i)
    {
        if (!matrix.Inverse(inverse) || !(*i)->m_Clip.Contains(inverse.Map(position)))
            inside = false;
        matrix = matrix * (*i)->m_Style.m_Transform.Resolve((*i)->m_Bounds);
        if (!matrix.Inverse(inverse))
            return {{-1000000, -1000000}, modifiers, clicks, false};
        if (*i != &widget && (*i)->m_Style.m_ClipContent &&
            !ContainsRounded((*i)->m_Bounds, (*i)->m_Style.m_Radius, inverse.Map(position)))
            inside = false;
    }
    return {inverse.Map(position), modifiers, clicks, inside};
}

Point UiContext::ToWindowPoint(const Widget& widget, Point point) const
{
    VerifyAccess();
    widget.VerifyAccess();
    if (widget.m_Context != this || !std::isfinite(point.m_X) || !std::isfinite(point.m_Y))
        throw std::invalid_argument("Window-coordinate conversion requires an attached widget and finite point.");
    std::vector<const Widget*> path;
    for (auto* node = &widget; node; node = node->m_Parent)
    {
        path.push_back(node);
        if (node->m_Overlay)
            break;
    }
    Affine matrix;
    for (auto node = path.rbegin(); node != path.rend(); ++node)
        matrix = matrix * (*node)->m_Style.m_Transform.Resolve((*node)->m_Bounds);
    return matrix.Map(point);
}

Widget* UiContext::HitTest(Widget& widget, Point position, Affine parentTransform) const
{
    if (!widget.m_Style.m_Displayed || !widget.m_Visible)
        return nullptr;
    Affine parentInverse;
    if (!parentTransform.Inverse(parentInverse) || !widget.m_Clip.Contains(parentInverse.Map(position)))
        return nullptr;
    const auto matrix = parentTransform * widget.m_Style.m_Transform.Resolve(widget.m_Bounds);
    Affine inverse;
    if (!matrix.Inverse(inverse))
        return nullptr;
    const auto local = inverse.Map(position);
    if (!ContainsRounded(widget.m_Bounds, widget.m_Style.m_ClipContent ? widget.m_Style.m_Radius : 0, local))
        return nullptr;
    for (auto i = widget.m_Children.rbegin(); i != widget.m_Children.rend(); ++i)
        if (!(*i)->m_Overlay)
            if (auto* found = HitTest(**i, position, matrix))
                return found;
    return ContainsRounded(widget.m_Bounds, widget.m_Style.m_Radius, local) ? &widget : nullptr;
}

bool UiContext::RefreshHover()
{
    if (!m_PointerPosition)
        return false;
    std::vector<Widget*> path;
    for (auto* node = InputHitTest(*m_PointerPosition); node; node = LogicalParent(*node))
        path.push_back(node);
    if (path.size() == m_HoverPath.size() &&
        std::equal(path.begin(), path.end(), m_HoverPath.begin(),
                   [](Widget* widget, const WidgetRef& previous) { return widget == previous.Get(); }))
        return false;
    for (const auto& reference : m_HoverPath)
        if (auto* previous = reference.Get(); previous && std::find(path.begin(), path.end(), previous) == path.end())
            previous->SetState(Hover, false);
    m_HoverPath.clear();
    for (auto* node : path)
    {
        node->SetState(Hover, true);
        m_HoverPath.emplace_back(node);
    }
    return true;
}

bool UiContext::RoutePointer(const PointerEvent& event)
{
    if (event.m_Device == PointerDevice::Mouse)
    {
        m_PointerPosition = event.m_Position;
        RefreshHover();
    }
    if (event.m_Type == PointerEventType::Cancel)
    {
        if (event.m_PointerId != m_CapturePointer || event.m_Device != m_CaptureDevice)
            return false;
        const auto captured = m_Capture;
        m_Capture = {};
        if (auto* live = captured.Get())
            return live->OnInput(event);
        return false;
    }
    if (event.m_Type == PointerEventType::Down && !m_Popups.empty())
    {
        std::size_t hit = m_Popups.size();
        for (std::size_t i = m_Popups.size(); i > 0; --i)
        {
            auto* popup = m_Popups[i - 1].m_Widget.Get();
            if (popup && HitTest(*popup, event.m_Position))
            {
                hit = i - 1;
                break;
            }
            if (!m_Popups[i - 1].m_Options.m_DismissOnOutside)
            {
                if (i < m_Popups.size())
                    ClosePopupBranch(i, {PopupCloseReason::OutsideClick, 0});
                return true;
            }
        }
        if (hit == m_Popups.size())
        {
            ClosePopupBranch(0, {PopupCloseReason::OutsideClick, 0});
            return true;
        }
        if (hit + 1 < m_Popups.size())
        {
            const auto parent = m_Popups[hit].m_Widget;
            ClosePopupBranch(hit + 1, {PopupCloseReason::OutsideClick, 0});
            if (GetPopup() != parent.Get())
                return true;
        }
    }
    const bool matchingCapture =
        m_Capture.Get() && event.m_PointerId == m_CapturePointer && event.m_Device == m_CaptureDevice;
    if (m_Capture.Get() && !matchingCapture)
        return false;
    WidgetRef target(event.m_Type == PointerEventType::Move && matchingCapture ? m_Capture.Get()
                                                                               : InputHitTest(event.m_Position));
    if (event.m_Type == PointerEventType::Up)
    {
        if (!matchingCapture || event.m_Button != m_CaptureButton)
            return false;
        target = m_Capture;
        m_Capture = {};
    }
    auto* widget = target.Get();
    if (!widget || !widget->IsEnabled())
        return false;
    try
    {
        if (PreviewInput(*widget, event) || !target.Get())
        {
            if (event.m_Type == PointerEventType::Up)
                if (auto* captured = target.Get())
                    captured->OnCaptureLost();
            return true;
        }
    }
    catch (...)
    {
        if (event.m_Type == PointerEventType::Up)
            if (auto* captured = target.Get())
                try
                {
                    captured->OnCaptureLost();
                }
                catch (...)
                {
                }
        throw;
    }
    if (event.m_Type == PointerEventType::Down)
    {
        if (event.m_Button == PointerButton::None || matchingCapture)
            return false;
        if (event.m_Button == PointerButton::Left)
            SetFocus(widget->IsFocusable() ? widget : nullptr);
        widget = target.Get();
        if (!widget || !IsWithin(widget, &InputRoot()))
            return true;
        m_Capture = target;
        m_CapturePointer = event.m_PointerId;
        m_CaptureDevice = event.m_Device;
        m_CaptureButton = event.m_Button;
    }
    widget = target.Get();
    auto local = event;
    const auto mapped = MakePointerEvent(*widget, event.m_Position, event.m_Modifiers, event.m_Clicks);
    local.m_Position = mapped.m_Position;
    local.m_InsideClip = mapped.m_InsideClip;
    bool handled = false;
    try
    {
        handled = widget->OnInput(local);
        if (!handled && event.m_Type == PointerEventType::Up)
            if (auto* captured = target.Get())
                captured->OnCaptureLost();
    }
    catch (...)
    {
        if (event.m_Type == PointerEventType::Up)
            if (auto* captured = target.Get())
                try
                {
                    captured->OnCaptureLost();
                }
                catch (...)
                {
                }
        throw;
    }
    if (!handled && event.m_Type == PointerEventType::Down)
    {
        if (m_Capture.Get() == target.Get())
            m_Capture = {};
        if (event.m_Button == PointerButton::Right)
        {
            ContextMenu(event.m_Position);
            return true;
        }
    }
    return handled;
}

void UiContext::PointerMove(Point position, Modifiers modifiers)
{
    PointerEvent event{position, modifiers};
    event.m_Timestamp = GetTime();
    DispatchInput(event);
}

void UiContext::PointerDown(Point position, Modifiers modifiers, int clicks)
{
    PointerEvent event{position, modifiers, clicks};
    event.m_Type = PointerEventType::Down;
    event.m_Button = PointerButton::Left;
    event.m_Buttons = 1;
    event.m_Timestamp = GetTime();
    DispatchInput(event);
}

void UiContext::PointerUp(Point position, Modifiers modifiers)
{
    PointerEvent event{position, modifiers};
    event.m_Type = PointerEventType::Up;
    event.m_Button = PointerButton::Left;
    event.m_Timestamp = GetTime();
    DispatchInput(event);
}

void UiContext::Scroll(Point position, Point delta, Modifiers modifiers)
{
    DispatchInput(ScrollEvent{position, delta, modifiers, GetTime()});
}

std::optional<Point> UiContext::GetPointerPosition() const
{
    VerifyAccess();
    return m_PointerPosition;
}

void UiContext::SetFocus(Widget* widget)
{
    const auto ownerLifetime = RetainOwner();
    VerifyDispatchPhase();
    if (widget && (!widget->IsEnabled() || !widget->IsFocusable() || widget->m_Context != this || !widget->IsVisible()))
        widget = nullptr;
    if (widget && !IsWithin(widget, &InputRoot()))
        return;
    if (m_Focus.Get() == widget)
        return;
    const WidgetRef next(widget);
    const auto previousFocus = m_Focus;
    CancelTextComposition();
    if (previousFocus.Get() != m_Focus.Get())
        return;
    m_Focus = next;
    Invalidate(m_Root, Invalidation::Selector);
    if (auto* previous = previousFocus.Get())
    {
        previous->SetState(Focus, false);
        previous->OnFocus(false);
    }
    if (m_Focus.Get() != next.Get())
        return;
    if (auto* current = next.Get())
    {
        current->SetState(Focus, true);
        current->OnFocus(true);
    }
}

void UiContext::FocusNext(bool backwards)
{
    Widget* scope = &InputRoot();
    for (auto* node = m_Focus.Get(); node && node != scope; node = LogicalParent(*node))
        if (node->m_FocusScope)
        {
            scope = node;
            break;
        }
    std::vector<WidgetRef> candidates;
    const auto collect = [&](auto&& self, Widget& widget) -> void
    {
        if (!widget.IsVisible() || !widget.IsEnabled())
            return;
        if (widget.IsFocusable())
            candidates.emplace_back(&widget);
        for (auto& child : widget.m_Children)
            if (!child->m_Overlay)
                self(self, *child);
    };
    collect(collect, *scope);
    if (candidates.empty())
    {
        SetFocus(nullptr);
        return;
    }
    const auto current = std::find_if(candidates.begin(), candidates.end(),
                                      [&](const WidgetRef& target) { return target.Get() == m_Focus.Get(); });
    const auto count = static_cast<std::ptrdiff_t>(candidates.size());
    const auto index = current == candidates.end() ? (backwards ? 0 : -1) : current - candidates.begin();
    SetFocus(candidates[static_cast<std::size_t>((index + (backwards ? -1 : 1) + count) % count)].Get());
}

void UiContext::ContextMenu(Point position)
{
    const auto ownerLifetime = RetainOwner();
    VerifyDispatchPhase();
    if (!std::isfinite(position.m_X) || !std::isfinite(position.m_Y))
        throw std::invalid_argument("Context-menu coordinates must be finite.");
    PrepareInput();
    std::vector<WidgetRef> path;
    const auto* boundary = &InputRoot();
    for (auto* node = InputHitTest(position); node; node = LogicalParent(*node))
    {
        path.emplace_back(node);
        if (node == boundary)
            break;
    }
    for (const auto& reference : path)
        if (auto* node = reference.Get(); node && node->IsEnabled() && node->m_ContextMenuRequested.HasConnections())
        {
            node->m_ContextMenuRequested.Emit(position);
            return;
        }
}

PopupSession UiContext::ShowPopup(Widget& popup, Point anchor, PopupOptions options)
{
    const auto ownerLifetime = RetainOwner();
    VerifyDispatchPhase();
    if (m_Deactivating)
        throw std::logic_error("A deactivating window cannot open a popup.");
    if (!std::isfinite(anchor.m_X) || !std::isfinite(anchor.m_Y))
        throw std::invalid_argument("Popup coordinates must be finite.");
    if (popup.GetContext() != this || &popup == &m_Root || popup.GetParent() != &m_Root)
        throw std::invalid_argument("Popups must be immediate owned children of this UI root.");
    const WidgetRef reference(&popup);
    WidgetRef parent(options.m_Parent);
    if (!parent.Get())
        for (const auto& owned : m_OwnedPopups)
            if (owned.m_Popup.Get() == &popup)
                for (auto entry = m_Popups.rbegin(); entry != m_Popups.rend(); ++entry)
                    if (IsWithin(owned.m_Owner.Get(), entry->m_Widget.Get()))
                    {
                        parent = entry->m_Widget;
                        break;
                    }
    std::size_t retain = 0;
    if (auto* target = parent.Get())
    {
        const auto found = std::find_if(m_Popups.begin(), m_Popups.end(),
                                        [&](const PopupEntry& entry) { return entry.m_Widget.Get() == target; });
        if (target == &popup || found == m_Popups.end())
            throw std::invalid_argument("A popup parent must be a different live presentation in this context.");
        retain = static_cast<std::size_t>(found - m_Popups.begin()) + 1;
    }
    if (retain >= 64)
        throw std::length_error("The popup stack is limited to 64 presentations.");
    auto session = PopupSession::Create();
    const auto revision = m_PopupRevision;
    if (retain < m_Popups.size())
        ClosePopupBranch(retain, {PopupCloseReason::Replaced, 0});
    if (!reference.Get() || (parent.Get() && !IsPopupOpen(*parent.Get())))
    {
        session.Complete({PopupCloseReason::OwnerExpired, 0});
        return session;
    }
    // Completion/focus observers own a newer presentation; never replace it as a side effect.
    if (m_Popups.size() != retain || m_PopupRevision > revision + 1)
    {
        session.Complete({PopupCloseReason::Replaced, 0});
        return session;
    }
    const auto captureRevision = m_PopupRevision;
    const auto previousCapture = m_Capture;
    m_Capture = {};
    if (auto* captured = previousCapture.Get())
        captured->OnCaptureLost();
    if (!reference.Get() || m_Popups.size() != retain || m_PopupRevision != captureRevision)
    {
        session.Complete({PopupCloseReason::Replaced, 0});
        return session;
    }
    options.m_Parent = nullptr;
    m_Popups.push_back({reference, parent, m_Focus, options, anchor, session});
    const auto current = ++m_PopupRevision;
    popup.m_Overlay = true;
    popup.SetVisible(true);
    Invalidate(m_Root, Invalidation::Selector | Invalidation::Arrange | Invalidation::Paint);
    PrepareInput();
    if (reference.Get() && session.IsPending() && current == m_PopupRevision)
    {
        SetFocus(reference.Get());
        if (!m_Focus.Get() && session.IsPending() && current == m_PopupRevision)
            FocusNext(false);
    }
    PrunePopups();
    return session;
}

Widget& UiContext::AddOwnedPopup(std::unique_ptr<Widget> popup, Widget& owner)
{
    const auto ownerLifetime = RetainOwner();
    VerifyDispatchPhase();
    if (!popup || owner.GetContext() != this)
        throw std::invalid_argument("An owned popup requires a live owner attached to this context.");
    popup->SetVisible(false);
    popup->m_Overlay = true;
    auto& result = m_Root.AddChild(std::move(popup));
    m_OwnedPopups.push_back({WidgetRef(&owner), WidgetRef(&result)});
    result.m_Node.m_Parent = &owner.m_Node;
    return result;
}

void UiContext::ReleaseSelectorOwner(const Widget& owner)
{
    for (const auto& entry : m_OwnedPopups)
        if (entry.m_Owner.Get() == &owner)
            if (auto* popup = entry.m_Popup.Get())
                popup->m_Node.m_Parent = popup->m_Parent ? &popup->m_Parent->m_Node : nullptr;
    for (const auto& entry : m_Popups)
        if (entry.m_Parent.Get() == &owner)
            if (auto* popup = entry.m_Widget.Get())
                popup->m_Node.m_Parent = popup->m_Parent ? &popup->m_Parent->m_Node : nullptr;
}

void UiContext::PruneOwnedPopups()
{
    std::vector<WidgetRef> expired;
    std::erase_if(m_OwnedPopups,
                  [&](const OwnedPopup& entry)
                  {
                      auto* owner = entry.m_Owner.Get();
                      if (!owner || owner->m_Context != this)
                      {
                          expired.push_back(entry.m_Popup);
                          return true;
                      }
                      return !entry.m_Popup.Get();
                  });
    for (const auto& reference : expired)
    {
        if (auto* popup = reference.Get())
            ClosePopup(*popup, {PopupCloseReason::OwnerExpired, 0});
        if (auto* popup = reference.Get())
            m_Root.RemoveChild(*popup);
    }
}

void UiContext::PrunePopups()
{
    for (std::size_t i = 0; i < m_Popups.size(); ++i)
    {
        auto* popup = m_Popups[i].m_Widget.Get();
        if (!popup || popup->m_Context != this || !popup->IsVisible() || !popup->IsEnabled())
        {
            ClosePopupBranch(i, {popup ? PopupCloseReason::Hidden : PopupCloseReason::OwnerExpired, 0});
            return;
        }
        for (const auto& owned : m_OwnedPopups)
            if (owned.m_Popup.Get() == popup)
                if (auto* owner = owned.m_Owner.Get(); !owner || !owner->IsVisible() || !owner->IsEnabled())
                {
                    ClosePopupBranch(i, {PopupCloseReason::OwnerExpired, 0});
                    return;
                }
    }
}

void UiContext::ClosePopupBranch(std::size_t index, PopupResult result)
{
    if (index >= m_Popups.size())
        return;
    std::vector<PopupEntry> closing;
    closing.reserve(m_Popups.size() - index);
    for (std::size_t i = index; i < m_Popups.size(); ++i)
        closing.push_back(std::move(m_Popups[i]));
    m_Popups.resize(index);
    ++m_PopupRevision;
    const auto restore = closing.front().m_ReturnFocus;
    const bool restoreFocus = closing.front().m_Options.m_RestoreFocus;
    const auto capture = m_Capture;
    bool cancelCapture = false;
    for (auto entry = closing.rbegin(); entry != closing.rend(); ++entry)
        if (auto* popup = entry->m_Widget.Get())
        {
            cancelCapture |= IsWithin(capture.Get(), popup);
            popup->SetVisible(false);
            auto* parent = LogicalParent(*popup);
            popup->m_Node.m_Parent = parent ? &parent->m_Node : nullptr;
        }
    if (cancelCapture)
        m_Capture = {};
    Invalidate(m_Root, Invalidation::Selector | Invalidation::Arrange | Invalidation::Paint);
    std::exception_ptr error;
    try
    {
        if (cancelCapture)
            if (auto* captured = capture.Get())
                captured->OnCaptureLost();
    }
    catch (...)
    {
        error = std::current_exception();
    }
    try
    {
        if (restoreFocus)
        {
            auto* target = restore.Get();
            while (target && (!target->IsVisible() || !target->IsEnabled() || !target->IsFocusable()))
                target = LogicalParent(*target);
            SetFocus(target);
            if (!m_Focus.Get() && !m_Popups.empty())
                FocusNext(false);
        }
        else if (!m_Focus.Get() || !IsWithin(m_Focus.Get(), &InputRoot()) || !m_Focus.Get()->IsVisible())
            SetFocus(nullptr);
    }
    catch (...)
    {
        if (!error)
            error = std::current_exception();
    }
    for (auto entry = closing.rbegin(); entry != closing.rend(); ++entry)
        entry->m_Session.Complete(entry == closing.rend() - 1 ? result
                                                              : PopupResult{PopupCloseReason::ParentClosed, 0});
    if (error)
        std::rethrow_exception(error);
}

void UiContext::ClosePopup(PopupResult result)
{
    const auto ownerLifetime = RetainOwner();
    VerifyDispatchPhase();
    if (!m_Popups.empty())
        ClosePopupBranch(m_Popups.size() - 1, result);
}

void UiContext::ClosePopup(Widget& popup, PopupResult result)
{
    const auto ownerLifetime = RetainOwner();
    VerifyDispatchPhase();
    const auto found = std::find_if(m_Popups.begin(), m_Popups.end(),
                                    [&](const PopupEntry& entry) { return entry.m_Widget.Get() == &popup; });
    if (found != m_Popups.end())
        ClosePopupBranch(static_cast<std::size_t>(found - m_Popups.begin()), result);
}

void UiContext::CloseAllPopups(PopupCloseReason reason)
{
    const auto ownerLifetime = RetainOwner();
    VerifyDispatchPhase();
    ClosePopupBranch(0, {reason, 0});
}

Widget* UiContext::GetPopup() const
{
    VerifyAccess();
    return m_Popups.empty() ? nullptr : m_Popups.back().m_Widget.Get();
}

Widget* UiContext::GetPopupParent(const Widget& popup) const
{
    VerifyAccess();
    for (const auto& entry : m_Popups)
        if (entry.m_Widget.Get() == &popup)
            return entry.m_Parent.Get();
    return nullptr;
}

bool UiContext::IsPopupOpen(const Widget& popup) const
{
    VerifyAccess();
    return std::any_of(m_Popups.begin(), m_Popups.end(),
                       [&](const PopupEntry& entry) { return entry.m_Widget.Get() == &popup; });
}

std::size_t UiContext::GetPopupCount() const
{
    VerifyAccess();
    return m_Popups.size();
}

void UiContext::Deactivate()
{
    const auto ownerLifetime = RetainOwner();
    VerifyDispatchPhase();
    const FlagScope deactivating(m_Deactivating);
    CloseAllPopups(PopupCloseReason::Deactivated);
    const auto capture = m_Capture;
    m_Capture = {};
    if (auto* captured = capture.Get())
        captured->OnCaptureLost();
    SetFocus(nullptr);
    for (const auto& reference : m_HoverPath)
        if (auto* widget = reference.Get())
            widget->SetState(Hover, false);
    m_HoverPath.clear();
    m_PointerPosition.reset();
}

void UiContext::SetClipboard(std::function<std::string()> get, std::function<void(std::string_view)> set)
{
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    m_GetClipboard = std::move(get);
    m_SetClipboard = std::move(set);
}

CursorShape UiContext::GetPointerCursor() const
{
    VerifyAccess();
    for (auto entry = m_HoverPath.begin(); entry != m_HoverPath.end(); ++entry)
        if (const auto* widget = entry->Get())
            if (const auto shape = widget->GetCursor())
                return *shape;
    return m_Root.GetCursor().value_or(CursorShape::Arrow);
}

std::string UiContext::GetClipboard() const
{
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    if (m_GetClipboard)
        return m_GetClipboard();
    auto result = const_cast<UiContext*>(this)->Platform().ReadClipboard();
    return result.m_Result ? result.m_Data.m_Text.value_or("") : std::string{};
}

void UiContext::SetClipboardText(std::string_view text) const
{
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    if (m_SetClipboard)
        m_SetClipboard(text);
    else
        const_cast<UiContext*>(this)->Platform().SetClipboardText(std::string(text));
}
} // namespace EStudioUI
