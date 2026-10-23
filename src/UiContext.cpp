#include <EStudioUI/UiContext.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/CssMotion.h>
#include <EStudioUI/BoxLayout.h>
#include <EStudioUI/MemoryPlatformServices.h>
#include <EStudioUI/SemanticTree.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>

namespace EStudioUI
{
struct UiContext::PhaseScope
{
    const UiContext& m_Context;
    UiPhase m_Previous;

    PhaseScope(const UiContext& context, UiPhase phase) : m_Context(context), m_Previous(context.m_Phase)
    {
        context.m_Phase = phase;
    }

    ~PhaseScope()
    {
        m_Context.m_Phase = m_Previous;
    }
};

UiContext::UiContext(FontFace& font)
    : m_Font(font), m_Animations(*this), m_CssMotion(std::make_unique<CssMotion>(*this))
{
    m_Font.VerifyAccess();
    m_FontRevision = m_Font.GetRevision();
    m_Root.Attach(this, nullptr);
    m_Dispatcher.SetDrainGuard([this] { VerifyTraversalEntry(); });
    m_Font.AttachHost(*this);
}

UiContext::~UiContext()
{
    if (!IsOwnerThread() || m_Phase != UiPhase::Idle)
        std::terminate();
    try
    {
        CancelTextComposition();
    }
    catch (...)
    {
        // Native delivery is already disconnected; extension cancellation cannot stop owner teardown.
        m_CompositionClient = {};
    }
    m_Closing = true;
    ClosePlatformServices();
    const auto popups = std::move(m_Popups);
    for (auto entry = popups.rbegin(); entry != popups.rend(); ++entry)
        entry->m_Session.Complete({PopupCloseReason::ContextClosed, 0});
    m_Dispatcher.Close();
    m_States.reset();
    m_Root.Attach(nullptr, nullptr);
    m_Font.DetachHost(*this);
}

void UiContext::VerifyAccess() const
{
    m_Affinity.VerifyAccess("UiContext access");
}

bool UiContext::IsOwnerThread() const noexcept
{
    return m_Affinity.IsOwnerThread();
}

void UiContext::SetOwnerLifetime(std::weak_ptr<void> owner)
{
    VerifyAccess();
    m_OwnerLifetime = owner;
    m_Dispatcher.SetOwnerLifetime(std::move(owner));
}

PlatformServices& UiContext::Platform()
{
    VerifyAccess();
    if (!m_Platform)
    {
        auto services = std::make_unique<MemoryPlatformServices>(m_Dispatcher);
        if (m_PlatformClosing || m_Closing)
        {
            services->Close();
            m_Platform = std::move(services);
        }
        else
            SetPlatformServices(std::move(services));
    }
    return *m_Platform;
}

void UiContext::SetPlatformServices(std::unique_ptr<PlatformServices> services)
{
    VerifyStructuralMutation();
    const auto ownerLifetime = RetainOwner();
    if (!services || m_PlatformClosing || m_Closing ||
        services->GetDispatcher().GetStopToken() != m_Dispatcher.GetStopToken() || services->IsClosed())
        throw std::invalid_argument("An active platform adapter must use this live context's dispatcher.");
    if (m_RetiredPlatforms.size() >= 8 && m_Platform && m_Platform->HasPendingNativeWork())
        throw std::length_error("This context already retains eight unwinding native adapters.");
    services->SetOperationGuard(
        [this]
        {
            VerifyDispatchPhase();
            if (StateStore::IsUpdatingOnThisThread())
                throw std::logic_error("Native services cannot observe a staged state transaction.");
        },
        m_OwnerLifetime);
    auto previous = std::move(m_Platform);
    m_Platform = std::move(services);
    if (previous)
    {
        if (previous->HasPendingNativeWork())
        {
            auto* retiring = previous.get();
            m_RetiredPlatforms.push_back(std::move(previous));
            retiring->Close();
        }
        else
            previous->Close();
    }
}

void UiContext::ClosePlatformServices()
{
    VerifyAccess();
    m_PlatformClosing = true;
    if (m_Platform)
        m_Platform->Close();
}

void UiContext::PumpPlatformServices()
{
    const auto ownerLifetime = RetainOwner();
    VerifyTraversalEntry();
    if (m_Platform)
        m_Platform->Pump();
    std::erase_if(m_RetiredPlatforms, [](const auto& services) { return !services->HasPendingNativeWork(); });
}

bool UiContext::HasPendingPlatformWork() const
{
    VerifyAccess();
    return (m_Platform && m_Platform->HasPendingNativeWork()) ||
           std::any_of(m_RetiredPlatforms.begin(), m_RetiredPlatforms.end(),
                       [](const auto& services) { return services->HasPendingNativeWork(); });
}

std::shared_ptr<void> UiContext::RetainOwner() const
{
    VerifyAccess();
    return m_OwnerLifetime.lock();
}

UiDispatcher UiContext::GetDispatcher() const
{
    VerifyAccess();
    return m_Dispatcher;
}

SemanticTree& UiContext::Semantics()
{
    VerifyTraversalEntry();
    // Create platform defaults before entering pure semantic inspection hooks.
    Platform();
    if (!m_Semantics)
        m_Semantics.reset(new SemanticTree(*this));
    return *m_Semantics;
}

StateStore& UiContext::States()
{
    VerifyAccess();
    if (!m_States)
    {
        m_States = std::make_unique<StateStore>(m_Dispatcher);
        m_States->SetAccessGuard([this] { VerifyDispatchPhase(); });
        m_States->SetOwnerLease([this] { return RetainOwner(); });
    }
    return *m_States;
}

DispatchReceipt UiContext::Defer(std::function<void(UiContext&)> callback)
{
    VerifyAccess();
    if (!callback)
        throw std::invalid_argument("A deferred UI commit requires a callback.");
    return m_Dispatcher.Post([this, callback = std::move(callback)] { callback(*this); });
}

UiPhase UiContext::GetPhase() const
{
    VerifyAccess();
    return m_Phase;
}

void UiContext::VerifyStructuralMutation() const
{
    VerifyDispatchPhase();
}

void UiContext::VerifyDispatchPhase() const
{
    VerifyAccess();
    if (m_Closing || m_Phase != UiPhase::Idle)
        throw std::logic_error(
            "UI traversal is active; defer structural changes with UiContext::Defer or UiHandle::Post.");
}

void UiContext::VerifyTraversalEntry(bool inputPreparation) const
{
    VerifyDispatchPhase();
    if (Detail::IsDeliveringInput() && !inputPreparation)
        throw std::logic_error("Input delivery is active; defer nested frame or queue traversal.");
    if (StateStore::IsUpdatingOnThisThread())
        throw std::logic_error("State update is active; defer frame traversal until the update completes.");
}

Widget& UiContext::Root()
{
    VerifyAccess();
    return m_Root;
}

std::uint64_t UiContext::GetTextInputRevision() const
{
    VerifyAccess();
    return m_TextInputRevision;
}

void UiContext::NotifyTextInputChanged()
{
    VerifyDispatchPhase();
    ++m_TextInputRevision;
    if (m_Semantics)
        m_Semantics->Changed();
}

TextInputState UiContext::GetTextInputState() const
{
    VerifyTraversalEntry();
    TextInputState state;
    state.m_Active = false;
    state.m_Epoch = m_TextInputEpoch;
    const auto* focused = m_Focus.Get();
    const auto* client = focused ? dynamic_cast<const TextInputClient*>(focused) : nullptr;
    if (!client || !focused->IsVisible() || !focused->IsEnabled() || !IsWithin(focused, &InputRoot()))
        return state;
    {
        const PhaseScope scope(*this, UiPhase::Inspection);
        state = client->GetTextInputState();
    }
    ValidatePlatformText(state.m_Text);
    if (state.m_Text.size() > 1024 * 1024 || state.m_Anchor > state.m_Text.size() ||
        state.m_Caret > state.m_Text.size())
        throw std::invalid_argument("A text input snapshot exceeds its bounded text or selection range.");
    for (const auto offset : {state.m_Anchor, state.m_Caret})
        if (offset < state.m_Text.size() && (static_cast<unsigned char>(state.m_Text[offset]) & 0xC0U) == 0x80U)
            throw std::invalid_argument("Text input selection must use UTF-8 code-point boundaries.");
    const auto caret = state.m_CaretBounds;
    if (!std::isfinite(caret.m_X) || !std::isfinite(caret.m_Y) || !std::isfinite(caret.m_Width) ||
        !std::isfinite(caret.m_Height) || caret.m_Width < 0 || caret.m_Height < 0)
        throw std::invalid_argument("Text input caret bounds must be finite and nonnegative.");
    Point points[4] = {{caret.m_X, caret.m_Y},
                       {caret.m_X + caret.m_Width, caret.m_Y},
                       {caret.m_X, caret.m_Y + caret.m_Height},
                       {caret.m_X + caret.m_Width, caret.m_Y + caret.m_Height}};
    for (auto& point : points)
        point = ToWindowPoint(*focused, point);
    float left = points[0].m_X, top = points[0].m_Y, right = left, bottom = top;
    for (const auto point : points)
    {
        left = std::min(left, point.m_X);
        top = std::min(top, point.m_Y);
        right = std::max(right, point.m_X);
        bottom = std::max(bottom, point.m_Y);
    }
    state.m_CaretBounds = {left, top, right - left, bottom - top};
    state.m_Client = focused->m_TargetKey;
    state.m_Epoch = m_TextInputEpoch;
    state.m_Active = state.m_Active && !state.m_ReadOnly;
    const auto metrics = const_cast<UiContext*>(this)->Platform().GetWindowMetrics();
    state.m_ScreenCaretBounds =
        metrics.m_HasScreenCoordinates ? std::optional<Rect>(metrics.ToScreen(state.m_CaretBounds)) : std::nullopt;
    return state;
}

void UiContext::Invalidate()
{
    VerifyAccess();
    ++m_TextInputRevision;
    if (m_Semantics)
        m_Semantics->Changed();
    m_Dirty = true;
    m_FullStyleDirty = true;
    m_ResetMeasurements = true;
    m_HitTestDirty = true;
    m_Root.m_SubtreeStyleDirty = true;
    m_Root.m_ArrangeDirty = true;
    m_Repaint = true;
}

void UiContext::Invalidate(Widget& widget, Invalidation flags)
{
    VerifyAccess();
    if (HasInvalidation(flags, Invalidation::Selector | Invalidation::InheritedStyle | Invalidation::Style |
                                   Invalidation::Measure | Invalidation::Arrange | Invalidation::HitTest |
                                   Invalidation::Structure))
        ++m_TextInputRevision;
    if (flags == Invalidation::None)
        return;
    if (m_Semantics)
        m_Semantics->Changed();
    m_Repaint = true;
    if (HasInvalidation(flags, Invalidation::Selector | Invalidation::Structure))
    {
        m_FullStyleDirty = true;
        m_Root.m_SubtreeStyleDirty = true;
    }
    if (HasInvalidation(flags, Invalidation::Style | Invalidation::InheritedStyle))
    {
        widget.m_StyleDirty = true;
        widget.m_DescendantStyleDirty |= HasInvalidation(flags, Invalidation::InheritedStyle);
        if (HasInvalidation(flags, Invalidation::InheritedStyle) && !m_OwnedPopups.empty())
            m_FullStyleDirty = true;
        for (auto* node = &widget; node; node = node->m_Parent)
            node->m_SubtreeStyleDirty = true;
    }
    if (HasInvalidation(flags, Invalidation::Measure | Invalidation::Structure))
    {
        widget.m_IntrinsicValid = false;
        for (auto* node = &widget; node; node = node->m_Parent)
            node->m_MeasureValid = false;
    }
    if (HasInvalidation(flags, Invalidation::Measure | Invalidation::Arrange | Invalidation::Structure))
        for (auto* node = &widget; node; node = node->m_Parent)
            node->m_ArrangeDirty = true;
    m_HitTestDirty |= HasInvalidation(flags, Invalidation::HitTest | Invalidation::Measure | Invalidation::Arrange |
                                                 Invalidation::Selector | Invalidation::Structure);
    m_Dirty |= flags != Invalidation::Paint;
}

void UiContext::RequestRepaint()
{
    VerifyAccess();
    m_Repaint = true;
}

bool UiContext::NeedsRepaint() const
{
    VerifyAccess();
    return m_Repaint || m_FontRevision != m_Font.GetRevision();
}

FontFace& UiContext::GetFont() const
{
    VerifyAccess();
    return m_Font;
}

void UiContext::SetTime(double time)
{
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    VerifyTraversalEntry();
    m_Animations.Update(time);
    m_CssMotion->Update(time);
    if (!m_Dirty)
        RefreshHover();
    UpdateToolTip(time);
}

double UiContext::GetTime() const
{
    VerifyAccess();
    return m_Animations.GetTime();
}

WidgetRef UiContext::Observe(Widget* widget) const
{
    VerifyAccess();
    return WidgetRef(widget);
}

Widget* UiContext::GetFocus() const
{
    VerifyAccess();
    return m_Focus.Get();
}

bool UiContext::SetStyleSheet(std::string_view css, std::string& error)
{
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    VerifyDispatchPhase();
    if (!m_StyleSheet.Parse(css, error))
        return false;
    m_Animations.Stop(m_StyleTransition, AnimationStopMode::ReleaseStyle);
    ++m_SheetGeneration;
    m_CssMotion->Reset();
    Invalidate();
    return true;
}

void UiContext::CopyStyleSheetFrom(const UiContext& source)
{
    const auto ownerLease = RetainOwner();
    const auto sourceLease = source.RetainOwner();
    VerifyDispatchPhase();
    source.VerifyAccess();
    if (&source == this)
        return;
    m_Animations.Stop(m_StyleTransition, AnimationStopMode::ReleaseStyle);
    m_StyleSheet = source.m_StyleSheet;
    ++m_SheetGeneration;
    m_CssMotion->Reset();
    Invalidate();
}

std::uint64_t UiContext::GetStyleSheetRevision() const
{
    VerifyAccess();
    return m_SheetGeneration;
}

Widget* UiContext::GetWidgetAt(Point point) const
{
    VerifyAccess();
    if (!std::isfinite(point.m_X) || !std::isfinite(point.m_Y))
        throw std::invalid_argument("Hit-test coordinates must be finite.");
    return InputHitTest(point);
}

bool UiContext::LoadStyleSheet(const std::filesystem::path& path, std::string& error)
{
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    VerifyDispatchPhase();
    if (!m_StyleSheet.LoadFile(path, error))
        return false;
    m_Animations.Stop(m_StyleTransition, AnimationStopMode::ReleaseStyle);
    ++m_SheetGeneration;
    m_CssMotion->Reset();
    Invalidate();
    return true;
}

AnimationSystem& UiContext::Animations()
{
    VerifyAccess();
    return m_Animations;
}

AnimationId UiContext::AnimateStyleSheet(std::string_view finalCss, const AnimationOptions& options, std::string& error)
{
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    VerifyDispatchPhase();
    options.Validate();
    StyleSheet final;
    if (!final.Parse(finalCss, error))
        return 0;
    return BeginStyleTransition(std::nullopt, std::move(final), options);
}

AnimationId UiContext::AnimateStyleSheet(std::string_view initialCss, std::string_view finalCss,
                                         const AnimationOptions& options, std::string& error)
{
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    VerifyDispatchPhase();
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
        ++m_SheetGeneration;
        m_CssMotion->Reset();
        Invalidate();
    }
    PrepareInput();

    struct Snapshot
    {
        WidgetRef m_Widget;
        Style m_Style;
        Rect m_Bounds;
        std::vector<std::vector<Style>> m_Parts;
    };

    std::vector<Snapshot> snapshots;
    const auto capture = [&](auto&& self, Widget& widget) -> void
    {
        Snapshot snapshot{WidgetRef(&widget), widget.m_Style, widget.m_Bounds, {}};
        for (const auto& part : widget.m_StyleParts)
        {
            auto palette = part.m_Palette;
            if (part.m_Animated)
                palette[Widget::PartIndex(part.m_StateMask, part.m_ActiveState)] = part.m_Presentation;
            snapshot.m_Parts.push_back(std::move(palette));
        }
        snapshots.push_back(std::move(snapshot));
        for (const auto& child : widget.m_Children)
            self(self, *child);
    };
    capture(capture, m_Root);
    const auto initialSheet = m_StyleSheet;
    m_Animations.Stop(m_StyleTransition, AnimationStopMode::ReleaseStyle);
    m_StyleSheet = std::move(final);
    ++m_SheetGeneration;
    m_CssMotion->Reset();
    Invalidate();
    Restyle(m_Root, nullptr, m_FullStyleDirty);
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

        // Each registered palette gets at most two tracks, independently of model row count.
        // Discrete values use the scheduler's endpoint progress, unaffected by easing overshoot.
        struct PartTween
        {
            std::size_t m_Variant;
            StyleProperty m_Property;
            StyleValue m_From, m_To;
        };

        for (std::size_t partIndex = 0; partIndex < snapshot.m_Parts.size(); ++partIndex)
        {
            auto& part = widget->m_StyleParts[partIndex];
            std::vector<PartTween> continuous, discrete;
            for (std::size_t variant = 0; variant < part.m_Palette.size(); ++variant)
            {
                const auto& firstStyle = snapshot.m_Parts[partIndex][variant];
                const auto& lastStyle = part.m_Palette[variant];
                for (int i = 0; i < static_cast<int>(StyleProperty::Count); ++i)
                {
                    const auto property = static_cast<StyleProperty>(i);
                    auto first = GetStyleValue(firstStyle, property);
                    auto last = GetStyleValue(lastStyle, property);
                    if (first == last)
                        continue;
                    bool isDiscrete = std::holds_alternative<bool>(first) || std::holds_alternative<std::string>(first);
                    if (const auto* length = std::get_if<Length>(&first))
                        isDiscrete = length->m_Unit != std::get<Length>(last).m_Unit;
                    (isDiscrete ? discrete : continuous)
                        .push_back({variant, property, std::move(first), std::move(last)});
                }
            }
            const auto addTrack = [&](std::vector<PartTween> values, bool isDiscrete)
            {
                if (values.empty())
                    return;
                auto tracks = std::make_shared<std::vector<PartTween>>(std::move(values));
                animation.AddCustom(*widget,
                                    "stylesheet-part:" + part.m_Name + (isDiscrete ? ":discrete" : ":continuous"),
                                    [this, partIndex, tracks](Widget& target, double progress)
                                    {
                                        auto& current = target.m_StyleParts[partIndex];
                                        bool geometryChanged = false;
                                        for (const auto& track : *tracks)
                                        {
                                            auto value = InterpolateStyleValue(track.m_From, track.m_To, progress);
                                            geometryChanged |= !IsPaintOnlyProperty(track.m_Property) &&
                                                               GetStyleValue(current.m_Palette[track.m_Variant],
                                                                             track.m_Property) != value;
                                            current.m_Overrides[track.m_Variant][track.m_Property] = std::move(value);
                                        }
                                        ApplyPartOverrides(target);
                                        if (geometryChanged)
                                            Invalidate();
                                        else
                                            RequestRepaint();
                                    });
                auto& track = animation.m_Tracks.back();
                track.m_Discrete = isDiscrete;
                track.m_ReleaseOnFinish = true;
                track.m_Release = [partIndex, tracks](Widget& target)
                {
                    for (const auto& item : *tracks)
                        target.m_StyleParts[partIndex].m_Overrides[item.m_Variant].erase(item.m_Property);
                    target.Invalidate();
                };
            };
            addTrack(std::move(continuous), false);
            addTrack(std::move(discrete), true);
        }
    }
    if (options.m_Alternate && options.m_Iterations && options.m_Iterations % 2 == 0)
        animation.m_OnFinish = [this, initialSheet]
        {
            m_StyleSheet = initialSheet;
            ++m_SheetGeneration;
            m_CssMotion->Reset();
            Invalidate();
        };
    const auto id = m_Animations.Play(std::move(animation));
    if (generation == m_StyleTransitionGeneration)
        m_StyleTransition = id;
    Invalidate();
    PrepareInput();
    return id;
}

void UiContext::Restyle(Widget& widget, const Style* parent, bool force)
{
    if (!force && !widget.m_SubtreeStyleDirty)
        return;
    const PhaseScope phase(*this, UiPhase::Style);
    const bool resolve = force || widget.m_StyleDirty;
    bool forceChildren = force || widget.m_DescendantStyleDirty;
    if (m_ResetMeasurements)
    {
        widget.m_IntrinsicValid = widget.m_MeasureValid = false;
        widget.m_ArrangeDirty = true;
    }
    if (resolve)
    {
        const auto previous = widget.m_Style;
        Invalidation changed = Invalidation::None;
        widget.m_StyleDirty = true;
        try
        {
            auto* logicalParent = LogicalParent(widget);
            widget.m_Node.m_Parent = logicalParent ? &logicalParent->m_Node : nullptr;
            if (widget.m_Overlay && logicalParent)
                parent = &logicalParent->m_Style;
            widget.m_Node.m_State &= ~FocusWithin;
            for (auto* focused = m_Focus.Get(); focused;)
            {
                if (focused == &widget)
                {
                    widget.m_Node.m_State |= FocusWithin;
                    break;
                }
                focused = LogicalParent(*focused);
            }
            if (!widget.m_Visible)
            {
                widget.m_Style.m_Displayed = false;
                widget.m_SubtreeStyleDirty = false;
                widget.m_DescendantStyleDirty = true;
                if (m_ResetMeasurements)
                {
                    const auto reset = [&](auto&& self, Widget& node) -> void
                    {
                        node.m_IntrinsicValid = node.m_MeasureValid = false;
                        node.m_ArrangeDirty = true;
                        for (auto& child : node.m_Children)
                            self(self, *child);
                    };
                    for (auto& child : widget.m_Children)
                        reset(reset, *child);
                }
                if (previous.m_Displayed)
                    Invalidate(widget, Invalidation::Content);
                return;
            }
            if (widget.IsEnabled())
                widget.m_Node.m_State &= ~Disabled;
            else
                widget.m_Node.m_State |= Disabled;
            widget.m_Style = m_CssMotion->Resolve(widget, 0, m_StyleSheet.Compute(widget.m_Node, parent), m_StyleSheet);
            widget.m_CascadedStyle = widget.m_Style;
            for (int layer = 1; layer <= 2; ++layer)
                m_CssMotion->Resolve(widget, layer, m_StyleSheet.Compute(widget.m_Node, &widget.m_Style, layer),
                                     m_StyleSheet);
            m_Stats.m_StyleResolutions += 3;
            for (const auto& [property, value] : widget.m_StyleOverrides)
                SetStyleValue(widget.m_Style, property, value);
            widget.m_Style.m_Displayed = widget.m_Style.m_Displayed && widget.m_Visible;
            widget.m_Style.m_MaxWidth = std::max(widget.m_Style.m_MaxWidth, widget.m_Style.m_MinWidth);
            widget.m_Style.m_MaxHeight = std::max(widget.m_Style.m_MaxHeight, widget.m_Style.m_MinHeight);
            const auto applySize =
                [](const std::optional<SizeRule>& rule, Length& length, float& minimum, float& maximum)
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
            applySize(widget.m_HeightRule, widget.m_Style.m_Height, widget.m_Style.m_MinHeight,
                      widget.m_Style.m_MaxHeight);
            if (widget.m_Direction)
                widget.m_Style.m_Row = *widget.m_Direction == Axis::Horizontal;
            if (widget.m_Gap)
                widget.m_Style.m_Gap = *widget.m_Gap;
            constexpr unsigned inherited =
                Disabled | Focus | FocusWithin | ReadOnly | Valid | Invalid | Intermediate | PlaceholderShown | Empty;
            for (std::size_t partIndex = 0; partIndex < widget.m_StyleParts.size(); ++partIndex)
            {
                auto& part = widget.m_StyleParts[partIndex];
                const unsigned common = widget.m_Node.m_State & (inherited | (part.m_Animated ? Hover | Pressed : 0u));
                part.m_ActiveState = part.m_State | common;
                for (std::size_t variant = 0; variant < part.m_States.size(); ++variant)
                {
                    const unsigned state = part.m_States[variant] | common;
                    part.m_Assignments[variant] = m_StyleSheet.CompilePart(widget.m_Node, part.m_Name, state);
                    auto style = part.m_Defaults(widget.m_Style, state);
                    style.m_FontFamily = widget.m_Style.m_FontFamily;
                    style.m_FontWeight = widget.m_Style.m_FontWeight;
                    style.m_FontStyle = widget.m_Style.m_FontStyle;
                    style.m_TextDirection = widget.m_Style.m_TextDirection;
                    style.m_Language = widget.m_Style.m_Language;
                    part.m_Assignments[variant](style);
                    if (!part.m_Animated)
                    {
                        style.m_Transitions.clear();
                        style.m_Animations.clear();
                    }
                    for (int index = 0; index < static_cast<int>(StyleProperty::Count); ++index)
                    {
                        const auto property = static_cast<StyleProperty>(index);
                        if (GetStyleValue(part.m_Palette[variant], property) != GetStyleValue(style, property))
                            changed |= GetStylePropertyInvalidation(property) & Invalidation::Content;
                    }
                    part.m_Palette[variant] = std::move(style);
                    ++m_Stats.m_StyleResolutions;
                }
                if (part.m_Animated)
                    part.m_Presentation = m_CssMotion->Resolve(
                        widget, static_cast<int>(partIndex + 3),
                        part.m_Palette[Widget::PartIndex(part.m_StateMask, part.m_ActiveState)], m_StyleSheet);
            }
            ApplyPartOverrides(widget);
            for (int index = 0; index < static_cast<int>(StyleProperty::Count); ++index)
            {
                const auto property = static_cast<StyleProperty>(index);
                if (GetStyleValue(previous, property) != GetStyleValue(widget.m_Style, property))
                    changed |= GetStylePropertyInvalidation(property);
            }
            forceChildren |= HasInvalidation(changed, Invalidation::InheritedStyle);
            Invalidate(widget, changed & (Invalidation::Content | Invalidation::HitTest | Invalidation::Paint));
            widget.m_StyleDirty = false;
        }
        catch (...)
        {
            Invalidate(widget, Invalidation::Style | Invalidation::InheritedStyle | Invalidation::Content);
            throw;
        }
    }
    for (auto& child : widget.m_Children)
        Restyle(*child, &widget.m_Style, forceChildren);
    widget.m_SubtreeStyleDirty = false;
    widget.m_DescendantStyleDirty = false;
}

void UiContext::RefreshStyleParts(Widget& widget)
{
    constexpr unsigned inherited =
        Disabled | Focus | FocusWithin | ReadOnly | Valid | Invalid | Intermediate | PlaceholderShown | Empty;
    for (std::size_t partIndex = 0; partIndex < widget.m_StyleParts.size(); ++partIndex)
    {
        auto& part = widget.m_StyleParts[partIndex];
        const unsigned common = widget.m_Node.m_State & (inherited | (part.m_Animated ? Hover | Pressed : 0u));
        for (std::size_t variant = 0; variant < part.m_States.size(); ++variant)
        {
            auto style = part.m_Defaults(widget.m_Style, part.m_States[variant] | common);
            if (part.m_Assignments[variant])
                part.m_Assignments[variant](style);
            if (!part.m_Animated)
            {
                style.m_Transitions.clear();
                style.m_Animations.clear();
            }
            part.m_Palette[variant] = std::move(style);
        }
        if (part.m_Animated)
            part.m_Presentation =
                m_CssMotion->Rebase(widget, static_cast<int>(partIndex + 3),
                                    part.m_Palette[Widget::PartIndex(part.m_StateMask, part.m_ActiveState)]);
    }
    ApplyPartOverrides(widget);
}

void UiContext::ApplyPartOverrides(Widget& widget)
{
    for (auto& part : widget.m_StyleParts)
    {
        for (std::size_t variant = 0; variant < part.m_Overrides.size(); ++variant)
            for (const auto& [property, value] : part.m_Overrides[variant])
                SetStyleValue(part.m_Palette[variant], property, value);
        if (part.m_Animated)
            for (const auto& [property, value] :
                 part.m_Overrides[Widget::PartIndex(part.m_StateMask, part.m_ActiveState)])
                SetStyleValue(part.m_Presentation, property, value);
    }
}

std::vector<Widget*> UiContext::LayoutChildren(Widget& widget) const
{
    std::vector<Widget*> children;
    for (auto& child : widget.m_Children)
        if (child->m_Style.m_Displayed && !child->m_Overlay && !child->m_FloatingBounds)
            children.push_back(child.get());
    return children;
}

LayoutPlan UiContext::Plan(Widget& widget, MeasureConstraints content, bool definiteWidth, bool definiteHeight)
{
    const PhaseScope phase(*this, UiPhase::Measure);
    const auto children = LayoutChildren(widget);
    LayoutContext context;
    context.m_Constraints = content;
    context.m_DefiniteWidth = definiteWidth && content.m_Width.has_value();
    context.m_DefiniteHeight = definiteHeight && content.m_Height.has_value();
    context.m_Gap = widget.m_Style.m_Gap;
    const auto& align = widget.m_Style.m_Align;
    context.m_Alignment = widget.m_Alignment.value_or(align == "start"      ? LayoutAlignment::Start
                                                      : align == "center"   ? LayoutAlignment::Center
                                                      : align == "end"      ? LayoutAlignment::End
                                                      : align == "baseline" ? LayoutAlignment::Baseline
                                                                            : LayoutAlignment::Stretch);
    const auto& justify = widget.m_Style.m_Justify;
    context.m_Justification =
        widget.m_Justification.value_or(justify == "center"          ? LayoutJustification::Center
                                        : justify == "end"           ? LayoutJustification::End
                                        : justify == "space-between" ? LayoutJustification::SpaceBetween
                                                                     : LayoutJustification::Start);
    for (const auto* child : children)
    {
        const auto& style = child->m_Style;
        context.m_Children.push_back({style.m_Width,
                                      style.m_Height,
                                      {style.m_MinWidth, style.m_MinHeight},
                                      {style.m_MaxWidth, style.m_MaxHeight},
                                      style.m_Margin,
                                      style.m_FlexGrow,
                                      child->m_TargetKey});
    }
    context.m_Measure = [&](std::size_t index, MeasureConstraints allocation, bool width, bool height)
    {
        if (index >= children.size())
            throw std::out_of_range("Layout strategy requested an unknown child.");
        return Measure(*children[index],
                       {context.m_DefiniteWidth ? content.m_Width : std::nullopt,
                        context.m_DefiniteHeight ? content.m_Height : std::nullopt},
                       allocation, width, height);
    };
    if (children.empty() && !widget.m_Layout)
        return {};
    const BoxLayout box(widget.m_Style.m_Row ? Axis::Horizontal : Axis::Vertical);
    const auto& strategy = widget.m_Layout ? *widget.m_Layout : static_cast<const LayoutStrategy&>(box);
    ++m_Stats.m_LayoutPlans;
    auto plan = strategy.Compute(context);
    const auto fail = [&](const char* message)
    {
        throw std::logic_error("Layout " + std::string(strategy.GetName()) + " #" + widget.m_Node.m_Id + ": " +
                               message);
    };
    plan.m_Measure.Validate();
    if (plan.m_Slots.size() != children.size())
        fail("return exactly one slot for each displayed, in-flow child.");
    std::vector<bool> visited(children.size());
    std::vector<unsigned char> reportedAxes(children.size());
    for (const auto& problem : plan.m_Problems)
        if (problem.m_Issue == LayoutIssue::Overflow && problem.m_Child && *problem.m_Child < children.size())
            reportedAxes[*problem.m_Child] |= problem.m_Axis == Axis::Horizontal ? 1 : 2;
    for (const auto& slot : plan.m_Slots)
    {
        if (slot.m_Index >= children.size() || visited[slot.m_Index])
            fail("child indices must be unique and in range.");
        visited[slot.m_Index] = true;
        slot.m_Measure.Validate();
        for (const float value : {slot.m_Bounds.m_X, slot.m_Bounds.m_Y, slot.m_Bounds.m_Width, slot.m_Bounds.m_Height})
            if (!std::isfinite(value))
                fail("allocated rectangles must be finite.");
        if (slot.m_Bounds.m_Width < 0 || slot.m_Bounds.m_Height < 0)
            fail("allocated dimensions must be nonnegative.");
        const auto& child = context.m_Children[slot.m_Index];
        if (slot.m_Bounds.m_Width <
                std::min(child.m_Maximum.m_X, std::max(child.m_Minimum.m_X, slot.m_Measure.m_Minimum.m_X)) - 0.01f ||
            slot.m_Bounds.m_Height <
                std::min(child.m_Maximum.m_Y, std::max(child.m_Minimum.m_Y, slot.m_Measure.m_Minimum.m_Y)) - 0.01f ||
            slot.m_Bounds.m_Width > child.m_Maximum.m_X + 0.01f || slot.m_Bounds.m_Height > child.m_Maximum.m_Y + 0.01f)
            fail("allocated rectangle violates an explicit hard size limit.");
        for (const bool width : {true, false})
        {
            const auto available = width ? content.m_Width : content.m_Height;
            if (!available || (reportedAxes[slot.m_Index] & (width ? 1 : 2)))
                continue;
            const float start = width ? slot.m_Bounds.m_X : slot.m_Bounds.m_Y;
            const float size = width ? slot.m_Bounds.m_Width : slot.m_Bounds.m_Height;
            const float after = width ? child.m_Margin.m_Right : child.m_Margin.m_Bottom;
            if (start < -0.01f || start + size + after > *available + 0.01f)
                plan.m_Problems.push_back(
                    {LayoutIssue::Overflow, width ? Axis::Horizontal : Axis::Vertical, slot.m_Index, *available,
                     std::max(size, start + size + after),
                     "Custom allocation exceeds available space; hard size limits are preserved."});
        }
    }
    for (const auto& problem : plan.m_Problems)
        if ((problem.m_Child && *problem.m_Child >= children.size()) || !std::isfinite(problem.m_Available) ||
            !std::isfinite(problem.m_Required))
            fail("diagnostics must refer to valid children and finite sizes.");
    for (const auto& track : plan.m_Tracks)
    {
        track.m_Rule.Validate();
        if (!std::isfinite(track.m_Preferred) || !std::isfinite(track.m_Minimum) || !std::isfinite(track.m_Allocated) ||
            track.m_Preferred < 0 || track.m_Minimum < 0 || track.m_Allocated < track.m_Minimum - 0.01f ||
            track.m_Allocated > track.m_Rule.m_Maximum + 0.01f)
            fail("track reports must contain finite sizes within their declared bounds.");
    }
    if (widget.m_BaselineChild)
    {
        plan.m_Measure.m_Baseline.reset();
        for (auto& slot : plan.m_Slots)
            if (children[slot.m_Index]->m_TargetKey == widget.m_BaselineChild)
            {
                slot.m_Measure = context.m_Measure(slot.m_Index, {slot.m_Bounds.m_Width, slot.m_Bounds.m_Height},
                                                   slot.m_DefiniteWidth, slot.m_DefiniteHeight);
                if (slot.m_Measure.m_Baseline)
                    plan.m_Measure.m_Baseline = slot.m_Bounds.m_Y + *slot.m_Measure.m_Baseline;
                break;
            }
    }
    plan.m_Measure.Validate();
    return plan;
}

MeasureResult UiContext::Measure(Widget& widget, MeasureConstraints parent, MeasureConstraints allocation,
                                 bool definiteWidth, bool definiteHeight)
{
    const PhaseScope phase(*this, UiPhase::Measure);
    const auto& style = widget.m_Style;
    parent.Validate();
    allocation.Validate();
    if (!style.m_Displayed)
        return {};
    if (style.m_Width.m_Unit != Length::Unit::Percent)
        parent.m_Width.reset();
    if (style.m_Height.m_Unit != Length::Unit::Percent)
        parent.m_Height.reset();
    if (style.m_MinWidth == style.m_MaxWidth)
        allocation.m_Width.reset();
    if (style.m_MinHeight == style.m_MaxHeight)
        allocation.m_Height.reset();
    definiteWidth &= allocation.m_Width.has_value();
    definiteHeight &= allocation.m_Height.has_value();
    if (!widget.m_MeasureValid)
        for (auto& entry : widget.m_MeasureCache)
            entry.m_Valid = false;
    for (const auto& entry : widget.m_MeasureCache)
        if (entry.m_Valid && entry.m_Parent == parent && entry.m_Allocation == allocation &&
            entry.m_DefiniteWidth == definiteWidth && entry.m_DefiniteHeight == definiteHeight)
        {
            ++m_Stats.m_MeasureCacheHits;
            widget.m_MeasuredSize = entry.m_Result.m_Preferred;
            widget.m_MeasuredResult = entry.m_Result;
            widget.m_ContentMeasurement = entry.m_Content;
            return entry.m_Result;
        }
    ++m_Stats.m_MeasureCalculations;
    widget.m_ArrangeDirty = true;
    if (!widget.m_IntrinsicValid)
    {
        ++m_Stats.m_Measurements;
        widget.m_IntrinsicSize = widget.Measure(m_Font);
        MeasureResult{{}, widget.m_IntrinsicSize, {}, false}.Validate();
        widget.m_IntrinsicValid = true;
    }
    const float paddingWidth = style.m_Padding.m_Left + style.m_Padding.m_Right + 2 * style.m_BorderWidth;
    const float paddingHeight = style.m_Padding.m_Top + style.m_Padding.m_Bottom + 2 * style.m_BorderWidth;
    const auto dimension = [](Length length, std::optional<float> reference, std::optional<float> assigned,
                              float minimum, float maximum) -> std::optional<float>
    {
        if (assigned)
            return std::clamp(*assigned, minimum, maximum);
        if (length.m_Unit == Length::Unit::Pixels || (length.m_Unit == Length::Unit::Percent && reference))
            return std::clamp(length.Resolve(reference.value_or(0), 0), minimum, maximum);
        return {};
    };
    const auto outerWidth =
        dimension(style.m_Width, parent.m_Width, allocation.m_Width, style.m_MinWidth, style.m_MaxWidth);
    const auto outerHeight =
        dimension(style.m_Height, parent.m_Height, allocation.m_Height, style.m_MinHeight, style.m_MaxHeight);
    MeasureConstraints content{
        outerWidth ? std::optional<float>(std::max(0.0f, *outerWidth - paddingWidth)) : std::nullopt,
        outerHeight ? std::optional<float>(std::max(0.0f, *outerHeight - paddingHeight)) : std::nullopt};
    const bool widthReference = style.m_Width.m_Unit == Length::Unit::Pixels || definiteWidth ||
                                (style.m_Width.m_Unit == Length::Unit::Percent && parent.m_Width);
    const bool heightReference = style.m_Height.m_Unit == Length::Unit::Pixels || definiteHeight ||
                                 (style.m_Height.m_Unit == Length::Unit::Percent && parent.m_Height);
    const auto measureContent = [&]
    {
        ++m_Stats.m_ConstrainedMeasurements;
        auto value = widget.MeasureConstrained(m_Font, content);
        value.Validate();
        return value;
    };
    auto raw = measureContent();
    auto plan = Plan(widget, content, widthReference, heightReference);
    const auto naturalWidth = [&]
    { return std::max(raw.m_Preferred.m_X, plan.m_Measure.m_Preferred.m_X) + paddingWidth; };
    const auto minimumWidth = [&]
    {
        return std::clamp(std::max(raw.m_Minimum.m_X, plan.m_Measure.m_Minimum.m_X) + paddingWidth, style.m_MinWidth,
                          style.m_MaxWidth);
    };
    float width = outerWidth.value_or(style.m_Width.m_Unit == Length::Unit::Fraction
                                          ? minimumWidth()
                                          : std::clamp(naturalWidth(), minimumWidth(), style.m_MaxWidth));
    // Width is resolved before height. A content/percentage cycle never triggers fixed-point iteration.
    if (!content.m_Width && style.m_Width.m_Unit != Length::Unit::Fraction)
    {
        content.m_Width = std::max(0.0f, width - paddingWidth);
        if (raw.m_HeightForWidth)
            raw = measureContent();
        if (!widget.m_Children.empty() || widget.m_Layout)
            plan = Plan(widget, content, widthReference, heightReference);
    }
    const float minimumHeight = std::clamp(std::max(raw.m_Minimum.m_Y, plan.m_Measure.m_Minimum.m_Y) + paddingHeight,
                                           style.m_MinHeight, style.m_MaxHeight);
    const float naturalHeight = std::max(raw.m_Preferred.m_Y, plan.m_Measure.m_Preferred.m_Y) + paddingHeight;
    const float height = outerHeight.value_or(style.m_Height.m_Unit == Length::Unit::Fraction
                                                  ? minimumHeight
                                                  : std::clamp(naturalHeight, minimumHeight, style.m_MaxHeight));
    MeasureResult result;
    result.m_Minimum = {minimumWidth(), minimumHeight};
    result.m_Preferred = {std::max(width, result.m_Minimum.m_X), std::max(height, minimumHeight)};
    result.m_HeightForWidth = raw.m_HeightForWidth || plan.m_Measure.m_HeightForWidth;
    if (const auto baseline = raw.m_Baseline ? raw.m_Baseline : plan.m_Measure.m_Baseline)
        result.m_Baseline = *baseline + style.m_Padding.m_Top + style.m_BorderWidth;
    result.Validate();
    auto& entry = widget.m_MeasureCache[widget.m_NextMeasureCache++ % widget.m_MeasureCache.size()];
    entry = {parent, allocation, definiteWidth, definiteHeight, result, raw, true};
    widget.m_MeasuredResult = result;
    widget.m_MeasuredSize = result.m_Preferred;
    widget.m_ContentMeasurement = raw;
    widget.m_MeasureValid = true;
    return result;
}

void UiContext::Layout(Widget& widget, Rect bounds, Rect clip, bool definiteWidth, bool definiteHeight)
{
    bounds.m_X += widget.m_Style.m_TranslateX;
    bounds.m_Y += widget.m_Style.m_TranslateY;
    const auto equal = [](Rect left, Rect right)
    {
        return left.m_X == right.m_X && left.m_Y == right.m_Y && left.m_Width == right.m_Width &&
               left.m_Height == right.m_Height;
    };
    if (!widget.m_ArrangeDirty && equal(widget.m_Bounds, bounds) && equal(widget.m_Clip, clip) &&
        widget.m_ArrangedDefiniteWidth == definiteWidth && widget.m_ArrangedDefiniteHeight == definiteHeight)
        return;
    ++m_Stats.m_Arrangements;
    widget.m_ArrangeDirty = true;
    widget.m_Bounds = bounds;
    widget.m_Clip = clip;
    widget.OnLayout();
    const auto content = widget.GetContentBounds();
    const auto childClip = clip.Intersect(content);
    const auto children = LayoutChildren(widget);
    const auto& style = widget.m_Style;
    const auto plan = Plan(widget, {content.m_Width, content.m_Height}, definiteWidth, definiteHeight);
    LayoutReport report;
    report.m_Strategy = widget.m_Layout ? std::string(widget.m_Layout->GetName()) : (style.m_Row ? "Row" : "Column");
    report.m_ContainerId = widget.m_Node.m_Id;
    report.m_ContentSize = {content.m_Width, content.m_Height};
    report.m_Measure = widget.m_MeasuredResult;
    report.m_ContentMeasure = widget.m_ContentMeasurement;
    report.m_Tracks = plan.m_Tracks;
    report.m_Constraints = plan.m_Constraints;
    for (const auto& slot : plan.m_Slots)
    {
        auto& child = *children[slot.m_Index];
        const auto& childStyle = child.m_Style;
        report.m_Children.push_back({child.m_TargetKey,
                                     child.m_Node.m_Id,
                                     {childStyle.m_Width,
                                      childStyle.m_Height,
                                      slot.m_Measure.m_Minimum,
                                      {childStyle.m_MaxWidth, childStyle.m_MaxHeight},
                                      childStyle.m_Margin,
                                      childStyle.m_FlexGrow,
                                      child.m_TargetKey},
                                     slot});
    }
    for (const auto& problem : plan.m_Problems)
    {
        const auto* child = problem.m_Child ? children[*problem.m_Child] : nullptr;
        report.m_Diagnostics.push_back({problem.m_Issue, problem.m_Axis, widget.m_TargetKey, widget.m_Node.m_Id,
                                        child ? child->m_TargetKey : 0, child ? child->m_Node.m_Id : std::string{},
                                        problem.m_Available, problem.m_Required, problem.m_Message});
    }
    const auto& raw = widget.m_ContentMeasurement;
    for (const bool width : {true, false})
    {
        const float required =
            width ? raw.m_Minimum.m_X : (raw.m_HeightForWidth ? raw.m_Preferred.m_Y : raw.m_Minimum.m_Y);
        const float available = width ? content.m_Width : content.m_Height;
        if (required > available + 0.01f)
            report.m_Diagnostics.push_back(
                {LayoutIssue::ContentClipped,
                 width ? Axis::Horizontal : Axis::Vertical,
                 widget.m_TargetKey,
                 widget.m_Node.m_Id,
                 0,
                 {},
                 available,
                 required,
                 "Content exceeds its explicit allocation; hard size limits are preserved and content is clipped."});
    }
    for (const auto& slot : plan.m_Slots)
    {
        auto childBounds = slot.m_Bounds;
        childBounds.m_X += content.m_X;
        childBounds.m_Y += content.m_Y;
        Layout(*children[slot.m_Index], childBounds, childClip, slot.m_DefiniteWidth, slot.m_DefiniteHeight);
    }
    for (auto& child : widget.m_Children)
        if (child->m_Style.m_Displayed && child->m_FloatingBounds && !child->m_Overlay)
        {
            const auto floating = *child->m_FloatingBounds;
            Measure(*child,
                    {definiteWidth ? std::optional<float>(content.m_Width) : std::nullopt,
                     definiteHeight ? std::optional<float>(content.m_Height) : std::nullopt},
                    {floating.m_Width, floating.m_Height}, true, true);
            Layout(*child, floating, childClip, true, true);
        }
    std::erase_if(m_OverflowingContainers,
                  [&widget](const WidgetRef& reference) { return !reference.Get() || reference.Get() == &widget; });
    widget.m_Overflowing =
        std::any_of(report.m_Diagnostics.begin(), report.m_Diagnostics.end(),
                    [](const LayoutDiagnostic& issue) { return issue.m_Issue != LayoutIssue::IndefinitePercentage; });
    if (widget.m_Overflowing)
        m_OverflowingContainers.emplace_back(&widget);
    widget.m_LayoutReport = std::move(report);
    widget.m_ArrangedDefiniteWidth = definiteWidth;
    widget.m_ArrangedDefiniteHeight = definiteHeight;
    widget.m_ArrangeDirty = !widget.m_MeasureValid;
}

void UiContext::FontsChanged()
{
    VerifyStructuralMutation();
    const PhaseScope phase(*this, UiPhase::Measure);
    const auto reset = [&](auto&& visit, Widget& widget) -> void
    {
        widget.OnFontsChanged();
        widget.m_IntrinsicValid = widget.m_MeasureValid = false;
        widget.m_ArrangeDirty = true;
        for (auto& child : widget.m_Children)
            visit(visit, *child);
    };
    reset(reset, m_Root);
    m_FontRevision = m_Font.GetRevision();
    m_Dirty = m_Repaint = m_HitTestDirty = true;
    ++m_TextInputRevision;
    if (m_Semantics)
        m_Semantics->Changed();
}

void UiContext::Prepare(float width, float height)
{
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    VerifyTraversalEntry(true);
    PruneOwnedPopups();
    if (m_FontRevision != m_Font.GetRevision())
        FontsChanged();
    width = std::max(1.0f, width);
    height = std::max(1.0f, height);
    if (width != m_Width || height != m_Height)
        Invalidate(m_Root, Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
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
    const bool fullStyle = m_FullStyleDirty;
    Restyle(m_Root, nullptr, fullStyle);
    m_FullStyleDirty = false;
    m_ResetMeasurements = false;
    PrunePopups();
    if (m_FullStyleDirty || m_Root.m_SubtreeStyleDirty)
        Restyle(m_Root, nullptr, m_FullStyleDirty);
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
        Restyle(m_Root, nullptr, m_FullStyleDirty);
    if (m_Root.m_ArrangeDirty || !m_Root.m_MeasureValid)
        Arrange();
    else if (m_HitTestDirty && !m_Popups.empty())
        ArrangePopups();
    m_HitTestDirty = false;
    if (RefreshHover())
    {
        const bool hoverStyle = m_FullStyleDirty;
        Restyle(m_Root, nullptr, hoverStyle);
        m_FullStyleDirty = false;
        if (m_Root.m_ArrangeDirty || !m_Root.m_MeasureValid)
            Arrange();
        else if (m_HitTestDirty && !m_Popups.empty())
            ArrangePopups();
    }
    m_Dirty = m_FullStyleDirty || m_Root.m_SubtreeStyleDirty || m_Root.m_ArrangeDirty || !m_Root.m_MeasureValid ||
              m_HitTestDirty;
    ++m_TextInputRevision;
}

void UiContext::Arrange()
{
    ++m_Stats.m_LayoutPasses;
    std::erase_if(m_OverflowingContainers,
                  [](const WidgetRef& reference)
                  {
                      const auto* widget = reference.Get();
                      return !widget || !widget->IsVisible();
                  });
    {
        const PhaseScope phase(*this, UiPhase::Measure);
        Measure(m_Root, {m_Width, m_Height}, {m_Width, m_Height}, true, true);
    }
    const PhaseScope phase(*this, UiPhase::Layout);
    Layout(m_Root, {0, 0, m_Width, m_Height}, {0, 0, m_Width, m_Height}, true, true);
    ArrangePopups();
}

void UiContext::ArrangePopups()
{
    const PhaseScope phase(*this, UiPhase::Layout);
    for (const auto& entry : m_Popups)
        if (auto* popup = entry.m_Widget.Get(); popup && popup->IsVisible())
        {
            auto measured = Measure(*popup, {m_Width, m_Height});
            const float popupWidth = std::max(measured.m_Minimum.m_X, std::min(m_Width, measured.m_Preferred.m_X));
            if (popupWidth != measured.m_Preferred.m_X)
                measured = Measure(*popup, {m_Width, m_Height}, {popupWidth, std::nullopt}, true, false);
            const float popupHeight = std::max(measured.m_Minimum.m_Y, std::min(m_Height, measured.m_Preferred.m_Y));
            Point origin = entry.m_Anchor;
            const auto placement = entry.m_Options.m_Placement;
            if (placement == PopupPlacement::Center)
                origin = {(m_Width - popupWidth) * 0.5f, (m_Height - popupHeight) * 0.5f};
            else if (const auto* anchor = entry.m_AnchorWidget.Get(); anchor && placement >= PopupPlacement::Below)
            {
                const auto b = anchor->GetBounds();
                const std::array<Point, 4> corners{ToWindowPoint(*anchor, {b.m_X, b.m_Y}),
                                                   ToWindowPoint(*anchor, {b.m_X + b.m_Width, b.m_Y}),
                                                   ToWindowPoint(*anchor, {b.m_X, b.m_Y + b.m_Height}),
                                                   ToWindowPoint(*anchor, {b.m_X + b.m_Width, b.m_Y + b.m_Height})};
                float left = corners[0].m_X, right = left, top = corners[0].m_Y, bottom = top;
                for (const auto point : corners)
                {
                    left = std::min(left, point.m_X);
                    right = std::max(right, point.m_X);
                    top = std::min(top, point.m_Y);
                    bottom = std::max(bottom, point.m_Y);
                }
                switch (placement)
                {
                case PopupPlacement::Below:
                    origin = {left, bottom};
                    if (bottom + popupHeight > m_Height && top >= popupHeight)
                        origin.m_Y = top - popupHeight;
                    break;
                case PopupPlacement::Above:
                    origin = {left, top - popupHeight};
                    if (origin.m_Y < 0 && bottom + popupHeight <= m_Height)
                        origin.m_Y = bottom;
                    break;
                case PopupPlacement::Right:
                    origin = {right, top};
                    if (right + popupWidth > m_Width && left >= popupWidth)
                        origin.m_X = left - popupWidth;
                    break;
                case PopupPlacement::Left:
                    origin = {left - popupWidth, top};
                    if (origin.m_X < 0 && right + popupWidth <= m_Width)
                        origin.m_X = right;
                    break;
                default:
                    break;
                }
            }
            Layout(*popup,
                   {std::clamp(origin.m_X, 0.0f, std::max(0.0f, m_Width - popupWidth)),
                    std::clamp(origin.m_Y, 0.0f, std::max(0.0f, m_Height - popupHeight)), popupWidth, popupHeight},
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
                            widget.m_Style.m_ClipContent, widget.m_Style.m_Radius,
                            reinterpret_cast<std::uintptr_t>(&widget));
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
        renderer.DrawDecoration(widget.m_Bounds, *after, widget.m_Clip, opacity, 2);
        renderer.PopState();
    }
    renderer.PopState();
}

void UiContext::Paint(Renderer& renderer, double time)
{
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    VerifyTraversalEntry();
    const PhaseScope phase(*this, UiPhase::Paint);
    m_Repaint = false;
    try
    {
        PaintTree(m_Root, renderer, time, 1);
        for (const auto& entry : m_Popups)
            if (auto* popup = entry.m_Widget.Get())
                PaintTree(*popup, renderer, time, 1);
    }
    catch (...)
    {
        m_Repaint = true;
        throw;
    }
}

const UiStats& UiContext::GetStats() const
{
    VerifyAccess();
    return m_Stats;
}

Point UiContext::ToLayoutPoint(const Widget& widget, Point point) const
{
    VerifyAccess();
    return MakePointerEvent(widget, point).m_Position;
}

void UiContext::PrepareSurfaces(double time, float scaleX, float scaleY)
{
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    VerifyTraversalEntry();
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0)
        throw std::invalid_argument("Surface pixel scales must be finite and positive.");
    const PhaseScope phase(*this, UiPhase::SurfacePreparation);
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
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    const PhaseScope phase(*this, UiPhase::Inspection);
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
    const auto ownerLifetime = RetainOwner();
    VerifyAccess();
    VerifyTraversalEntry();
    const PhaseScope phase(*this, UiPhase::Submission);
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
    VerifyAccess();
    return m_OverflowingContainers;
}

std::vector<LayoutDiagnostic> UiContext::GetLayoutDiagnostics() const
{
    VerifyAccess();
    std::vector<LayoutDiagnostic> diagnostics;
    const auto visit = [&](auto&& self, const Widget& widget) -> void
    {
        if (!widget.m_Style.m_Displayed)
            return;
        diagnostics.insert(diagnostics.end(), widget.m_LayoutReport.m_Diagnostics.begin(),
                           widget.m_LayoutReport.m_Diagnostics.end());
        for (const auto& child : widget.m_Children)
            self(self, *child);
    };
    visit(visit, m_Root);
    return diagnostics;
}
} // namespace EStudioUI
