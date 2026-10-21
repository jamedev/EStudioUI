#include <EStudioUI/UiContext.h>
#include <EStudioUI/Renderer.h>

#include <algorithm>
#include <cmath>

namespace EStudioUI
{
UiContext::UiContext(FontFace& font) : m_Font(font)
{
    m_Root.Attach(this, nullptr);
}

UiContext::~UiContext()
{
    m_Root.Attach(nullptr, nullptr);
}

Widget& UiContext::Root()
{
    return m_Root;
}

void UiContext::Invalidate()
{
    m_Dirty = true;
    m_Repaint = true;
}

void UiContext::RequestRepaint()
{
    m_Repaint = true;
}

bool UiContext::NeedsRepaint() const
{
    return m_Repaint;
}

FontFace& UiContext::GetFont() const
{
    return m_Font;
}

void UiContext::SetTime(double time)
{
    m_Time = time;
}

double UiContext::GetTime() const
{
    return m_Time;
}

WidgetRef UiContext::Observe(Widget* widget) const
{
    return WidgetRef(widget);
}

Widget* UiContext::GetFocus() const
{
    return m_Focus.Get();
}

bool UiContext::SetStyleSheet(std::string_view css, std::string& error)
{
    if (!m_StyleSheet.Parse(css, error))
        return false;
    Invalidate();
    return true;
}

bool UiContext::LoadStyleSheet(const std::filesystem::path& path, std::string& error)
{
    if (!m_StyleSheet.LoadFile(path, error))
        return false;
    Invalidate();
    return true;
}

void UiContext::Restyle(Widget& widget, const Style* parent)
{
    if (widget.IsEnabled())
        widget.m_Node.m_State &= ~Disabled;
    else
        widget.m_Node.m_State |= Disabled;
    widget.m_Style = m_StyleSheet.Compute(widget.m_Node, parent);
    for (auto& child : widget.m_Children)
        Restyle(*child, &widget.m_Style);
}

Point UiContext::Measure(Widget& widget, float parentWidth, float parentHeight)
{
    const auto& style = widget.m_Style;
    if (!style.m_Displayed)
        return {};
    Point size = widget.Measure(m_Font);
    float main = 0;
    float cross = 0;
    int count = 0;
    const float innerWidth =
        std::max(0.0f, parentWidth - style.m_Padding.m_Left - style.m_Padding.m_Right - 2 * style.m_BorderWidth);
    const float innerHeight =
        std::max(0.0f, parentHeight - style.m_Padding.m_Top - style.m_Padding.m_Bottom - 2 * style.m_BorderWidth);
    for (auto& child : widget.m_Children)
    {
        if (!child->m_Style.m_Displayed)
            continue;
        const auto measured = Measure(*child, innerWidth, innerHeight);
        const auto& margin = child->m_Style.m_Margin;
        const float width = measured.m_X + margin.m_Left + margin.m_Right;
        const float height = measured.m_Y + margin.m_Top + margin.m_Bottom;
        main += style.m_Row ? width : height;
        cross = std::max(cross, style.m_Row ? height : width);
        ++count;
    }
    if (count > 1)
        main += static_cast<float>(count - 1) * style.m_Gap;
    size.m_X = std::max(size.m_X, style.m_Row ? main : cross);
    size.m_Y = std::max(size.m_Y, style.m_Row ? cross : main);
    size.m_X += style.m_Padding.m_Left + style.m_Padding.m_Right + 2 * style.m_BorderWidth;
    size.m_Y += style.m_Padding.m_Top + style.m_Padding.m_Bottom + 2 * style.m_BorderWidth;
    size.m_X = std::clamp(style.m_Width.Resolve(parentWidth, size.m_X), style.m_MinWidth, style.m_MaxWidth);
    size.m_Y = std::clamp(style.m_Height.Resolve(parentHeight, size.m_Y), style.m_MinHeight, style.m_MaxHeight);
    return size;
}

void UiContext::Layout(Widget& widget, Rect bounds, Rect clip)
{
    widget.m_Bounds = bounds;
    widget.m_Clip = clip;
    const auto content = widget.GetContentBounds();
    const auto childClip = clip.Intersect(content);
    const auto& style = widget.m_Style;

    struct Item
    {
        Widget* m_Widget;
        Point m_Size;
    };

    std::vector<Item> items;
    float used = 0;
    float grow = 0;
    for (auto& child : widget.m_Children)
    {
        if (!child->m_Style.m_Displayed)
            continue;
        const auto size = Measure(*child, content.m_Width, content.m_Height);
        const auto& margin = child->m_Style.m_Margin;
        used += style.m_Row ? size.m_X + margin.m_Left + margin.m_Right : size.m_Y + margin.m_Top + margin.m_Bottom;
        grow += child->m_Style.m_FlexGrow;
        items.push_back({child.get(), size});
    }
    float gap = style.m_Gap;
    if (items.size() > 1)
        used += gap * static_cast<float>(items.size() - 1);
    const float available = style.m_Row ? content.m_Width : content.m_Height;
    const float extra = std::max(0.0f, available - used);
    float position = style.m_Row ? content.m_X : content.m_Y;
    if (grow == 0)
    {
        if (style.m_Justify == "center")
            position += extra * 0.5f;
        else if (style.m_Justify == "end")
            position += extra;
        else if (style.m_Justify == "space-between" && items.size() > 1)
            gap += extra / static_cast<float>(items.size() - 1);
    }
    for (auto& item : items)
    {
        auto& child = *item.m_Widget;
        const auto& childStyle = child.m_Style;
        const auto& margin = childStyle.m_Margin;
        float main = style.m_Row ? item.m_Size.m_X : item.m_Size.m_Y;
        if (grow > 0)
            main += extra * childStyle.m_FlexGrow / grow;
        main = std::clamp(main, style.m_Row ? childStyle.m_MinWidth : childStyle.m_MinHeight,
                          style.m_Row ? childStyle.m_MaxWidth : childStyle.m_MaxHeight);
        const float before = style.m_Row ? margin.m_Left : margin.m_Top;
        const float after = style.m_Row ? margin.m_Right : margin.m_Bottom;
        const float crossBefore = style.m_Row ? margin.m_Top : margin.m_Left;
        const float crossAfter = style.m_Row ? margin.m_Bottom : margin.m_Right;
        const float crossAvailable =
            std::max(0.0f, (style.m_Row ? content.m_Height : content.m_Width) - crossBefore - crossAfter);
        float cross = style.m_Row ? item.m_Size.m_Y : item.m_Size.m_X;
        const auto crossUnit = style.m_Row ? childStyle.m_Height.m_Unit : childStyle.m_Width.m_Unit;
        if (style.m_Align == "stretch" && crossUnit == Length::Unit::Auto)
            cross = crossAvailable;
        cross = std::clamp(cross, style.m_Row ? childStyle.m_MinHeight : childStyle.m_MinWidth,
                           style.m_Row ? childStyle.m_MaxHeight : childStyle.m_MaxWidth);
        float crossPosition = (style.m_Row ? content.m_Y : content.m_X) + crossBefore;
        if (style.m_Align == "center")
            crossPosition += (crossAvailable - cross) * 0.5f;
        else if (style.m_Align == "end")
            crossPosition += crossAvailable - cross;
        position += before;
        const Rect childBounds =
            style.m_Row ? Rect{position, crossPosition, main, cross} : Rect{crossPosition, position, cross, main};
        Layout(child, childBounds, childClip);
        position += main + after + gap;
    }
}

void UiContext::Prepare(float width, float height)
{
    width = std::max(1.0f, width);
    height = std::max(1.0f, height);
    if (width != m_Width || height != m_Height)
        Invalidate();
    m_Width = width;
    m_Height = height;
    if (auto* focused = m_Focus.Get(); focused && !focused->IsEnabled())
        SetFocus(nullptr);
    if (auto* capture = m_Capture.Get(); capture && !capture->IsEnabled())
    {
        capture->OnCaptureLost();
        m_Capture = {};
    }
    if (!m_Dirty)
        return;
    Restyle(m_Root, nullptr);
    const auto visible = [](Widget* widget)
    {
        for (auto* node = widget; node; node = node->m_Parent)
            if (!node->m_Style.m_Displayed)
                return false;
        return true;
    };
    bool statesChanged = false;
    if (auto* focused = m_Focus.Get(); focused && !visible(focused))
    {
        SetFocus(nullptr);
        statesChanged = true;
    }
    if (auto* captured = m_Capture.Get(); captured && !visible(captured))
    {
        m_Capture = {};
        captured->OnCaptureLost();
        statesChanged = true;
    }
    if (statesChanged)
        Restyle(m_Root, nullptr);
    Layout(m_Root, {0, 0, width, height}, {0, 0, width, height});
    m_Dirty = false;
}

void UiContext::PaintTree(Widget& widget, Renderer& renderer, double time, float opacity)
{
    if (!widget.m_Style.m_Displayed || widget.m_Clip.m_Width <= 0 || widget.m_Clip.m_Height <= 0)
        return;
    opacity *= widget.m_Style.m_Opacity;
    widget.Paint(renderer, m_Font, time, opacity);
    for (auto& child : widget.m_Children)
        PaintTree(*child, renderer, time, opacity);
}

void UiContext::Paint(Renderer& renderer, double time)
{
    m_Time = time;
    PaintTree(m_Root, renderer, time, 1);
    m_Repaint = false;
}

Widget* UiContext::HitTest(Widget& widget, Point position) const
{
    if (!widget.m_Style.m_Displayed || !widget.m_Bounds.Contains(position) || !widget.m_Clip.Contains(position))
        return nullptr;
    for (auto i = widget.m_Children.rbegin(); i != widget.m_Children.rend(); ++i)
        if (auto* found = HitTest(**i, position))
            return found;
    return &widget;
}

void UiContext::PointerMove(Point position, Modifiers modifiers)
{
    Prepare(m_Width, m_Height);
    auto* target = HitTest(m_Root, position);
    std::vector<Widget*> path;
    for (auto* node = target; node; node = node->m_Parent)
        path.push_back(node);
    for (const auto& reference : m_HoverPath)
        if (auto* previous = reference.Get(); previous && std::find(path.begin(), path.end(), previous) == path.end())
            previous->SetState(Hover, false);
    m_HoverPath.clear();
    for (auto* node : path)
    {
        node->SetState(Hover, true);
        m_HoverPath.emplace_back(node);
    }
    if (auto* capture = m_Capture.Get())
        capture->OnPointerMove({position, modifiers, 1});
}

void UiContext::PointerDown(Point position, Modifiers modifiers, int clicks)
{
    const auto previousCapture = m_Capture;
    m_Capture = {};
    if (auto* previous = previousCapture.Get())
        previous->OnCaptureLost();
    PointerMove(position, modifiers);
    Prepare(m_Width, m_Height);
    auto* target = HitTest(m_Root, position);
    if (!target || !target->IsEnabled())
        return;
    const WidgetRef reference(target);
    SetFocus(target->IsFocusable() ? target : nullptr);
    if (auto* live = reference.Get())
    {
        m_Capture = reference;
        live->OnPointerDown({position, modifiers, clicks});
    }
}

void UiContext::PointerUp(Point position, Modifiers modifiers)
{
    Prepare(m_Width, m_Height);
    const auto captured = m_Capture;
    m_Capture = {};
    if (auto* live = captured.Get())
        live->OnPointerUp({position, modifiers, 1});
    PointerMove(position, modifiers);
}

void UiContext::SetFocus(Widget* widget)
{
    if (widget && (!widget->IsEnabled() || !widget->IsFocusable() || widget->m_Context != this))
        widget = nullptr;
    if (m_Focus.Get() == widget)
        return;
    const WidgetRef next(widget);
    if (auto* previous = m_Focus.Get())
    {
        previous->SetState(Focus, false);
        previous->OnFocus(false);
    }
    m_Focus = next;
    if (auto* current = next.Get())
    {
        current->SetState(Focus, true);
        current->OnFocus(true);
    }
}

void UiContext::FocusNext(bool backwards)
{
    std::vector<Widget*> candidates;
    const auto collect = [&](auto&& self, Widget& widget) -> void
    {
        if (!widget.m_Style.m_Displayed || !widget.IsEnabled())
            return;
        if (widget.IsFocusable())
            candidates.push_back(&widget);
        for (auto& child : widget.m_Children)
            self(self, *child);
    };
    collect(collect, m_Root);
    if (candidates.empty())
    {
        SetFocus(nullptr);
        return;
    }
    const auto current = std::find(candidates.begin(), candidates.end(), m_Focus.Get());
    const auto count = static_cast<std::ptrdiff_t>(candidates.size());
    const auto index = current == candidates.end() ? (backwards ? 0 : -1) : current - candidates.begin();
    SetFocus(candidates[static_cast<std::size_t>((index + (backwards ? -1 : 1) + count) % count)]);
}

void UiContext::KeyEvent(Key key, bool down, bool repeat, Modifiers modifiers)
{
    Prepare(m_Width, m_Height);
    if (key == Key::Tab && down)
    {
        if (!repeat)
            FocusNext(modifiers.m_Shift);
        return;
    }
    if (auto* focused = m_Focus.Get(); focused && focused->IsEnabled())
        focused->OnKey(key, down, repeat, modifiers);
}

void UiContext::TextInput(std::string_view text)
{
    Prepare(m_Width, m_Height);
    if (auto* focused = m_Focus.Get(); focused && focused->IsEnabled())
        focused->OnText(text);
}

void UiContext::Deactivate()
{
    if (auto* captured = m_Capture.Get())
        captured->OnCaptureLost();
    m_Capture = {};
    SetFocus(nullptr);
    for (auto& reference : m_HoverPath)
        if (auto* widget = reference.Get())
            widget->SetState(Hover, false);
    m_HoverPath.clear();
}

void UiContext::SetClipboard(std::function<std::string()> get, std::function<void(std::string_view)> set)
{
    m_GetClipboard = std::move(get);
    m_SetClipboard = std::move(set);
}

std::string UiContext::GetClipboard() const
{
    return m_GetClipboard ? m_GetClipboard() : std::string{};
}

void UiContext::SetClipboardText(std::string_view text) const
{
    if (m_SetClipboard)
        m_SetClipboard(text);
}
} // namespace EStudioUI
