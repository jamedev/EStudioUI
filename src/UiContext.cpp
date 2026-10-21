#include <EStudioUI/UiContext.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/CssMotion.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
UiContext::UiContext(FontFace& font)
    : m_Font(font), m_Animations(*this), m_CssMotion(std::make_unique<CssMotion>(*this))
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
    m_Animations.Update(time);
    m_CssMotion->Update(time);
    if (!m_Dirty)
        RefreshHover();
}

double UiContext::GetTime() const
{
    return m_Animations.GetTime();
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
    m_Animations.Stop(m_StyleTransition, AnimationStopMode::ReleaseStyle);
    m_CssMotion->Reset();
    Invalidate();
    return true;
}

bool UiContext::LoadStyleSheet(const std::filesystem::path& path, std::string& error)
{
    if (!m_StyleSheet.LoadFile(path, error))
        return false;
    m_Animations.Stop(m_StyleTransition, AnimationStopMode::ReleaseStyle);
    m_CssMotion->Reset();
    Invalidate();
    return true;
}

AnimationSystem& UiContext::Animations()
{
    return m_Animations;
}

AnimationId UiContext::AnimateStyleSheet(std::string_view finalCss, const AnimationOptions& options, std::string& error)
{
    options.Validate();
    StyleSheet final;
    if (!final.Parse(finalCss, error))
        return 0;
    return BeginStyleTransition(std::nullopt, std::move(final), options);
}

AnimationId UiContext::AnimateStyleSheet(std::string_view initialCss, std::string_view finalCss,
                                         const AnimationOptions& options, std::string& error)
{
    options.Validate();
    StyleSheet initial;
    StyleSheet final;
    if (!initial.Parse(initialCss, error) || !final.Parse(finalCss, error))
        return 0;
    return BeginStyleTransition(std::move(initial), std::move(final), options);
}

//! Endpoint sheets are resolved once, including inheritance and intrinsic sizing.
//! Only changed properties become tracks. Normal CSS remains active underneath,
//! and explicit layout contracts are reapplied after presentation overrides.
AnimationId UiContext::BeginStyleTransition(std::optional<StyleSheet> initial, StyleSheet final,
                                            const AnimationOptions& options)
{
    const auto generation = ++m_StyleTransitionGeneration;
    if (initial)
    {
        m_Animations.Stop(m_StyleTransition, AnimationStopMode::ReleaseStyle);
        m_StyleSheet = std::move(*initial);
        m_CssMotion->Reset();
        Invalidate();
    }
    Prepare(m_Width, m_Height);

    struct Snapshot
    {
        WidgetRef m_Widget;
        Style m_Style;
        Rect m_Bounds;
    };

    std::vector<Snapshot> snapshots;
    const auto capture = [&](auto&& self, Widget& widget) -> void
    {
        snapshots.push_back({WidgetRef(&widget), widget.m_Style, widget.m_Bounds});
        for (const auto& child : widget.m_Children)
            self(self, *child);
    };
    capture(capture, m_Root);
    const auto initialSheet = m_StyleSheet;
    m_Animations.Stop(m_StyleTransition, AnimationStopMode::ReleaseStyle);
    m_StyleSheet = std::move(final);
    m_CssMotion->Reset();
    Invalidate();
    Restyle(m_Root, nullptr);
    Arrange();
    Animation animation(options);
    // A sentinel also gives no-change transitions a valid timeline identity.
    animation.AddCustom(m_Root, "stylesheet", [](Widget&, double) {});
    for (const auto& snapshot : snapshots)
    {
        auto* widget = snapshot.m_Widget.Get();
        if (!widget || widget->GetContext() != this)
            continue;
        auto from = snapshot.m_Style;
        auto to = widget->m_Style;
        bool fixedMainAxis = false;
        for (auto property : {StyleProperty::Width, StyleProperty::Height})
        {
            auto& first = property == StyleProperty::Width ? from.m_Width : from.m_Height;
            auto& last = property == StyleProperty::Width ? to.m_Width : to.m_Height;
            if (first.m_Unit == last.m_Unit)
                continue;
            first = {Length::Unit::Pixels,
                     property == StyleProperty::Width ? snapshot.m_Bounds.m_Width : snapshot.m_Bounds.m_Height};
            last = {Length::Unit::Pixels,
                    property == StyleProperty::Width ? widget->m_Bounds.m_Width : widget->m_Bounds.m_Height};
            if (widget->m_Parent && widget->m_Parent->m_Style.m_Row == (property == StyleProperty::Width))
                fixedMainAxis = true;
        }
        if (fixedMainAxis)
            from.m_FlexGrow = to.m_FlexGrow = 0;
        for (int i = 0; i < static_cast<int>(StyleProperty::Count); ++i)
        {
            const auto property = static_cast<StyleProperty>(i);
            auto first = GetStyleValue(from, property);
            auto last = GetStyleValue(to, property);
            if (first != last || (fixedMainAxis && property == StyleProperty::FlexGrow))
                animation.AddStyleProperty(*widget, property, std::move(first), std::move(last), true);
        }
    }
    if (options.m_Alternate && options.m_Iterations && options.m_Iterations % 2 == 0)
        animation.m_OnFinish = [this, initialSheet]
        {
            m_StyleSheet = initialSheet;
            m_CssMotion->Reset();
            Invalidate();
        };
    const auto id = m_Animations.Play(std::move(animation));
    if (generation == m_StyleTransitionGeneration)
        m_StyleTransition = id;
    Invalidate();
    Prepare(m_Width, m_Height);
    return id;
}

void UiContext::Restyle(Widget& widget, const Style* parent)
{
    if (widget.IsEnabled())
        widget.m_Node.m_State &= ~Disabled;
    else
        widget.m_Node.m_State |= Disabled;
    widget.m_Style = m_CssMotion->Resolve(widget, 0, m_StyleSheet.Compute(widget.m_Node, parent), m_StyleSheet);
    for (int layer = 1; layer <= 2; ++layer)
        m_CssMotion->Resolve(widget, layer, m_StyleSheet.Compute(widget.m_Node, &widget.m_Style, layer), m_StyleSheet);
    m_Stats.m_StyleResolutions += 3;
    for (const auto& [property, value] : widget.m_StyleOverrides)
        SetStyleValue(widget.m_Style, property, value);
    widget.m_Style.m_Displayed = widget.m_Style.m_Displayed && widget.m_Visible;
    widget.m_Style.m_MaxWidth = std::max(widget.m_Style.m_MaxWidth, widget.m_Style.m_MinWidth);
    widget.m_Style.m_MaxHeight = std::max(widget.m_Style.m_MaxHeight, widget.m_Style.m_MinHeight);
    const auto applySize = [](const std::optional<SizeRule>& rule, Length& length, float& minimum, float& maximum)
    {
        if (!rule)
            return;
        minimum = rule->m_Minimum;
        maximum = rule->m_Maximum;
        length = rule->m_Mode == SizeRule::Mode::Fixed  ? Length{Length::Unit::Pixels, rule->m_Value}
                 : rule->m_Mode == SizeRule::Mode::Fill ? Length{Length::Unit::Fraction, rule->m_Value}
                                                        : Length{};
    };
    applySize(widget.m_WidthRule, widget.m_Style.m_Width, widget.m_Style.m_MinWidth, widget.m_Style.m_MaxWidth);
    applySize(widget.m_HeightRule, widget.m_Style.m_Height, widget.m_Style.m_MinHeight, widget.m_Style.m_MaxHeight);
    if (widget.m_Direction)
        widget.m_Style.m_Row = *widget.m_Direction == Axis::Horizontal;
    if (widget.m_Gap)
        widget.m_Style.m_Gap = *widget.m_Gap;
    for (auto& child : widget.m_Children)
        Restyle(*child, &widget.m_Style);
}

Point UiContext::Measure(Widget& widget, float parentWidth, float parentHeight)
{
    const auto& style = widget.m_Style;
    if (!style.m_Displayed)
        return {};
    Point size = widget.Measure(m_Font);
    const float width = style.m_Width.Resolve(parentWidth, parentWidth);
    const float height = style.m_Height.Resolve(parentHeight, parentHeight);
    const float innerWidth =
        std::max(0.0f, width - style.m_Padding.m_Left - style.m_Padding.m_Right - 2 * style.m_BorderWidth);
    const float innerHeight =
        std::max(0.0f, height - style.m_Padding.m_Top - style.m_Padding.m_Bottom - 2 * style.m_BorderWidth);
    float main = 0;
    float cross = 0;
    int count = 0;
    for (auto& child : widget.m_Children)
    {
        if (!child->m_Style.m_Displayed)
            continue;
        const auto measured = Measure(*child, innerWidth, innerHeight);
        if (child->m_Overlay || child->m_FloatingBounds)
            continue;
        const auto& margin = child->m_Style.m_Margin;
        const float childWidth = measured.m_X + margin.m_Left + margin.m_Right;
        const float childHeight = measured.m_Y + margin.m_Top + margin.m_Bottom;
        main += style.m_Row ? childWidth : childHeight;
        cross = std::max(cross, style.m_Row ? childHeight : childWidth);
        ++count;
    }
    if (count > 1)
        main += static_cast<float>(count - 1) * style.m_Gap;
    size.m_X = std::max(size.m_X, style.m_Row ? main : cross) + style.m_Padding.m_Left + style.m_Padding.m_Right +
               2 * style.m_BorderWidth;
    size.m_Y = std::max(size.m_Y, style.m_Row ? cross : main) + style.m_Padding.m_Top + style.m_Padding.m_Bottom +
               2 * style.m_BorderWidth;
    size.m_X =
        std::clamp(style.m_Width.m_Unit == Length::Unit::Fraction ? 0.0f : style.m_Width.Resolve(parentWidth, size.m_X),
                   style.m_MinWidth, style.m_MaxWidth);
    size.m_Y = std::clamp(
        style.m_Height.m_Unit == Length::Unit::Fraction ? 0.0f : style.m_Height.Resolve(parentHeight, size.m_Y),
        style.m_MinHeight, style.m_MaxHeight);
    m_Measurements[&widget] = size;
    return size;
}

void UiContext::Layout(Widget& widget, Rect bounds, Rect clip)
{
    bounds.m_X += widget.m_Style.m_TranslateX;
    bounds.m_Y += widget.m_Style.m_TranslateY;
    widget.m_Bounds = bounds;
    widget.m_Clip = clip;
    widget.OnLayout();
    const auto content = widget.GetContentBounds();
    const auto childClip = clip.Intersect(content);
    const auto& style = widget.m_Style;
    std::vector<Widget*> children;
    std::vector<LayoutItem> items;
    float margins = 0;
    for (auto& child : widget.m_Children)
    {
        const auto& childStyle = child->m_Style;
        if (!childStyle.m_Displayed || child->m_Overlay || child->m_FloatingBounds)
            continue;
        auto size = m_Measurements[child.get()];
        if (childStyle.m_Width.m_Unit == Length::Unit::Percent)
            size.m_X = std::clamp(childStyle.m_Width.Resolve(content.m_Width, size.m_X), childStyle.m_MinWidth,
                                  childStyle.m_MaxWidth);
        if (childStyle.m_Height.m_Unit == Length::Unit::Percent)
            size.m_Y = std::clamp(childStyle.m_Height.Resolve(content.m_Height, size.m_Y), childStyle.m_MinHeight,
                                  childStyle.m_MaxHeight);
        m_Measurements[child.get()] = size;
        const auto& dimension = style.m_Row ? childStyle.m_Width : childStyle.m_Height;
        const auto& margin = childStyle.m_Margin;
        margins += style.m_Row ? margin.m_Left + margin.m_Right : margin.m_Top + margin.m_Bottom;
        items.push_back({style.m_Row ? size.m_X : size.m_Y,
                         style.m_Row ? childStyle.m_MinWidth : childStyle.m_MinHeight,
                         style.m_Row ? childStyle.m_MaxWidth : childStyle.m_MaxHeight,
                         dimension.m_Unit == Length::Unit::Fraction ? dimension.m_Value : childStyle.m_FlexGrow});
        children.push_back(child.get());
    }
    float gap = style.m_Gap;
    const float gaps = children.empty() ? 0 : gap * static_cast<float>(children.size() - 1);
    const float available = style.m_Row ? content.m_Width : content.m_Height;
    const auto allocation = EStudioUI::Layout::Distribute(available - margins - gaps, items);
    if (allocation.m_Overflow > 0.01f || margins + gaps > available + 0.01f)
        m_OverflowingContainers.emplace_back(&widget);
    float position = style.m_Row ? content.m_X : content.m_Y;
    if (style.m_Justify == "center")
        position += allocation.m_Unused * 0.5f;
    else if (style.m_Justify == "end")
        position += allocation.m_Unused;
    else if (style.m_Justify == "space-between" && children.size() > 1)
        gap += allocation.m_Unused / static_cast<float>(children.size() - 1);
    for (std::size_t i = 0; i < children.size(); ++i)
    {
        auto& child = *children[i];
        const auto& childStyle = child.m_Style;
        const auto& margin = childStyle.m_Margin;
        const auto size = m_Measurements[&child];
        const float before = style.m_Row ? margin.m_Left : margin.m_Top;
        const float after = style.m_Row ? margin.m_Right : margin.m_Bottom;
        const float crossBefore = style.m_Row ? margin.m_Top : margin.m_Left;
        const float crossAfter = style.m_Row ? margin.m_Bottom : margin.m_Right;
        const float crossAvailable =
            std::max(0.0f, (style.m_Row ? content.m_Height : content.m_Width) - crossBefore - crossAfter);
        const auto crossUnit = style.m_Row ? childStyle.m_Height.m_Unit : childStyle.m_Width.m_Unit;
        float cross = style.m_Row ? size.m_Y : size.m_X;
        if (crossUnit == Length::Unit::Fraction || (style.m_Align == "stretch" && crossUnit == Length::Unit::Auto))
            cross = crossAvailable;
        cross = std::clamp(cross, style.m_Row ? childStyle.m_MinHeight : childStyle.m_MinWidth,
                           style.m_Row ? childStyle.m_MaxHeight : childStyle.m_MaxWidth);
        float crossPosition = (style.m_Row ? content.m_Y : content.m_X) + crossBefore;
        if (style.m_Align == "center")
            crossPosition += (crossAvailable - cross) * 0.5f;
        else if (style.m_Align == "end")
            crossPosition += crossAvailable - cross;
        position += before;
        const Rect childBounds = style.m_Row ? Rect{position, crossPosition, allocation.m_Sizes[i], cross}
                                             : Rect{crossPosition, position, cross, allocation.m_Sizes[i]};
        Layout(child, childBounds, childClip);
        position += allocation.m_Sizes[i] + after + gap;
    }
    for (auto& child : widget.m_Children)
        if (child->m_Style.m_Displayed && child->m_FloatingBounds && !child->m_Overlay)
            Layout(*child, *child->m_FloatingBounds, childClip);
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
    if (auto* popup = m_Popup.Get(); popup && (!popup->IsVisible() || !popup->IsEnabled()))
    {
        ClosePopup();
        Restyle(m_Root, nullptr);
    }
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
    Arrange();
    m_Dirty = false;
    if (RefreshHover())
    {
        Restyle(m_Root, nullptr);
        Arrange();
        m_Dirty = false;
    }
}

void UiContext::Arrange()
{
    ++m_Stats.m_LayoutPasses;
    m_Measurements.clear();
    m_OverflowingContainers.clear();
    Measure(m_Root, m_Width, m_Height);
    Layout(m_Root, {0, 0, m_Width, m_Height}, {0, 0, m_Width, m_Height});
    if (auto* popup = m_Popup.Get(); popup && popup->IsVisible())
    {
        const auto measured = m_Measurements[popup];
        const float popupWidth = std::min(m_Width, measured.m_X);
        const float popupHeight = std::min(m_Height, measured.m_Y);
        Layout(*popup,
               {std::clamp(m_PopupAnchor.m_X, 0.0f, m_Width - popupWidth),
                std::clamp(m_PopupAnchor.m_Y, 0.0f, m_Height - popupHeight), popupWidth, popupHeight},
               {0, 0, m_Width, m_Height});
    }
}

void UiContext::PaintTree(Widget& widget, Renderer& renderer, double time, float opacity)
{
    if (!widget.m_Style.m_Displayed || widget.m_Clip.m_Width <= 0 || widget.m_Clip.m_Height <= 0)
        return;
    opacity *= widget.m_Style.m_Opacity;
    if (opacity <= 0)
        return;
    renderer.PushState(widget.m_Style.m_Transform.Resolve(widget.m_Bounds));
    renderer.SetWidgetPaint(widget.m_Bounds, widget.m_Clip, m_CssMotion->Decoration(widget, 1),
                            widget.m_Style.m_ClipContent, widget.m_Style.m_Radius);
    widget.Paint(renderer, m_Font, time, opacity);
    renderer.PushState();
    if (widget.m_Style.m_ClipContent)
        renderer.PushClip(widget.m_Bounds, widget.m_Style.m_Radius);
    renderer.PushClip(widget.GetContentBounds());
    for (auto& child : widget.m_Children)
        if (!child->m_Overlay)
            PaintTree(*child, renderer, time, opacity);
    renderer.PopState();
    if (const auto* after = m_CssMotion->Decoration(widget, 2))
    {
        renderer.PushState();
        if (widget.m_Style.m_ClipContent)
            renderer.PushClip(widget.m_Bounds, widget.m_Style.m_Radius);
        renderer.DrawDecoration(widget.m_Bounds, *after, widget.m_Clip, opacity);
        renderer.PopState();
    }
    renderer.PopState();
}

void UiContext::Paint(Renderer& renderer, double time)
{
    PaintTree(m_Root, renderer, time, 1);
    if (auto* popup = m_Popup.Get())
        PaintTree(*popup, renderer, time, 1);
    m_Repaint = false;
}

const UiStats& UiContext::GetStats() const
{
    return m_Stats;
}

Point UiContext::ToLayoutPoint(const Widget& widget, Point point) const
{
    return MakePointerEvent(widget, point).m_Position;
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

Widget* UiContext::HitTest(Widget& widget, Point position, Affine parentTransform) const
{
    if (!widget.m_Style.m_Displayed)
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
    auto* target = HitTest(m_Popup.Get() ? *m_Popup.Get() : m_Root, *m_PointerPosition);
    std::vector<Widget*> path;
    for (auto* node = target; node; node = node->m_Parent)
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

void UiContext::PointerMove(Point position, Modifiers modifiers)
{
    m_PointerPosition = position;
    Prepare(m_Width, m_Height);
    RefreshHover();
    auto* target = HitTest(m_Popup.Get() ? *m_Popup.Get() : m_Root, position);
    if (auto* capture = m_Capture.Get())
        capture->OnPointerMove(MakePointerEvent(*capture, position, modifiers));
    else if (target && target->IsEnabled())
        target->OnPointerMove(MakePointerEvent(*target, position, modifiers));
}

void UiContext::PointerDown(Point position, Modifiers modifiers, int clicks)
{
    Prepare(m_Width, m_Height);
    if (auto* popup = m_Popup.Get(); popup && !popup->GetBounds().Contains(ToLayoutPoint(*popup, position)))
    {
        ClosePopup();
        return;
    }
    const auto previousCapture = m_Capture;
    m_Capture = {};
    if (auto* previous = previousCapture.Get())
        previous->OnCaptureLost();
    PointerMove(position, modifiers);
    Prepare(m_Width, m_Height);
    auto* target = HitTest(m_Popup.Get() ? *m_Popup.Get() : m_Root, position);
    if (!target || !target->IsEnabled())
        return;
    const WidgetRef reference(target);
    SetFocus(target->IsFocusable() ? target : nullptr);
    if (auto* live = reference.Get())
    {
        m_Capture = reference;
        live->OnPointerDown(MakePointerEvent(*live, position, modifiers, clicks));
    }
}

void UiContext::PointerUp(Point position, Modifiers modifiers)
{
    Prepare(m_Width, m_Height);
    const auto captured = m_Capture;
    m_Capture = {};
    if (auto* live = captured.Get())
        live->OnPointerUp(MakePointerEvent(*live, position, modifiers));
    PointerMove(position, modifiers);
}

void UiContext::SetFocus(Widget* widget)
{
    if (widget && (!widget->IsEnabled() || !widget->IsFocusable() || widget->m_Context != this || !widget->IsVisible()))
        widget = nullptr;
    if (m_Focus.Get() == widget)
        return;
    const WidgetRef next(widget);
    const auto previousFocus = m_Focus;
    m_Focus = next;
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
    std::vector<Widget*> candidates;
    const auto collect = [&](auto&& self, Widget& widget) -> void
    {
        if (!widget.IsVisible() || !widget.IsEnabled())
            return;
        if (widget.IsFocusable())
            candidates.push_back(&widget);
        for (auto& child : widget.m_Children)
            self(self, *child);
    };
    collect(collect, m_Popup.Get() ? *m_Popup.Get() : m_Root);
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
    if (down && key == Key::Escape && m_Popup.Get())
    {
        ClosePopup();
        return;
    }
    if (down && key == Key::Menu && m_Focus.Get())
    {
        const auto bounds = m_Focus.Get()->GetBounds();
        ContextMenu({bounds.m_X + bounds.m_Width * 0.5f, bounds.m_Y + bounds.m_Height * 0.5f});
        return;
    }
    std::vector<WidgetRef> preview;
    for (auto* node = m_Focus.Get(); node; node = node->m_Parent)
        preview.emplace_back(node);
    for (auto& reference : preview)
        if (auto* node = reference.Get(); node && node->OnKeyPreview(key, down, repeat, modifiers))
            return;
    if (key == Key::Tab && down)
    {
        Prepare(m_Width, m_Height);
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
    ClosePopup();
    if (auto* captured = m_Capture.Get())
        captured->OnCaptureLost();
    m_Capture = {};
    SetFocus(nullptr);
    for (auto& reference : m_HoverPath)
        if (auto* widget = reference.Get())
            widget->SetState(Hover, false);
    m_HoverPath.clear();
    m_PointerPosition.reset();
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

namespace EStudioUI
{
void UiContext::Scroll(Point position, Point delta, Modifiers modifiers)
{
    Prepare(m_Width, m_Height);
    std::vector<WidgetRef> path;
    for (auto* node = HitTest(m_Popup.Get() ? *m_Popup.Get() : m_Root, position); node; node = node->m_Parent)
        path.emplace_back(node);
    for (auto& reference : path)
        if (auto* node = reference.Get(); node && node->IsEnabled() && node->OnScroll(delta, modifiers))
            return;
}

void UiContext::ContextMenu(Point position)
{
    ClosePopup();
    Prepare(m_Width, m_Height);
    std::vector<WidgetRef> path;
    for (auto* node = HitTest(m_Root, position); node; node = node->m_Parent)
        path.emplace_back(node);
    for (auto& reference : path)
        if (auto* node = reference.Get(); node && node->IsEnabled() && node->m_ContextMenuRequested.HasConnections())
        {
            node->m_ContextMenuRequested.Emit(position);
            return;
        }
}

void UiContext::ShowPopup(Widget& popup, Point anchor)
{
    if (!std::isfinite(anchor.m_X) || !std::isfinite(anchor.m_Y))
        throw std::invalid_argument("Popup coordinates must be finite.");
    if (popup.GetContext() != this || &popup == &m_Root || popup.GetParent() != &m_Root)
        throw std::invalid_argument("Popups must be immediate owned children of this UI root.");
    const WidgetRef popupRef(&popup);
    ClosePopup();
    if (!popupRef.Get())
        return;
    m_PopupReturnFocus = m_Focus;
    m_Popup = WidgetRef(&popup);
    m_PopupAnchor = anchor;
    popup.m_Overlay = true;
    popup.SetVisible(true);
    Invalidate();
    Prepare(m_Width, m_Height);
    SetFocus(&popup);
}

void UiContext::ClosePopup()
{
    const auto popup = m_Popup;
    const auto restore = m_PopupReturnFocus;
    m_Popup = {};
    m_PopupReturnFocus = {};
    if (auto* live = popup.Get())
    {
        live->SetVisible(false);
        const auto capture = m_Capture;
        m_Capture = {};
        if (auto* captured = capture.Get())
            captured->OnCaptureLost();
        auto* target = restore.Get();
        while (target && (!target->IsVisible() || !target->IsEnabled() || !target->IsFocusable()))
            target = target->m_Parent;
        SetFocus(target);
    }
    Invalidate();
}

Widget* UiContext::GetPopup() const
{
    return m_Popup.Get();
}

void UiContext::PrepareSurfaces(double time, float scaleX, float scaleY)
{
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0)
        throw std::invalid_argument("Surface pixel scales must be finite and positive.");
    const auto visit = [&](auto&& self, Widget& widget) -> void
    {
        if (!widget.IsVisible() || widget.GetBounds().Intersect(widget.GetClip()).m_Width <= 0 ||
            widget.GetBounds().Intersect(widget.GetClip()).m_Height <= 0)
            return;
        widget.PrepareFrame(time, scaleX, scaleY);
        for (auto& child : widget.m_Children)
            self(self, *child);
    };
    visit(visit, m_Root);
}

bool UiContext::HasAnimations() const
{
    if (m_Animations.HasRunning() || m_CssMotion->HasRunning())
        return true;
    const auto visit = [&](auto&& self, const Widget& widget) -> bool
    {
        if (!widget.IsVisible() || widget.GetBounds().Intersect(widget.GetClip()).m_Width <= 0 ||
            widget.GetBounds().Intersect(widget.GetClip()).m_Height <= 0)
            return false;
        if (widget.NeedsContinuousRendering())
            return true;
        return std::any_of(widget.m_Children.begin(), widget.m_Children.end(),
                           [&](const auto& child) { return self(self, *child); });
    };
    return visit(visit, m_Root);
}

void UiContext::FrameSubmitted()
{
    const auto visit = [&](auto&& self, Widget& widget) -> void
    {
        if (!widget.IsVisible())
            return;
        widget.FrameSubmitted();
        for (auto& child : widget.m_Children)
            self(self, *child);
    };
    visit(visit, m_Root);
}

const std::vector<WidgetRef>& UiContext::GetOverflowingContainers() const
{
    return m_OverflowingContainers;
}
} // namespace EStudioUI
