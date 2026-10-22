#pragma once

#include "Signal.h"
#include "Style.h"
#include "Layout.h"
#include "SizeRule.h"
#include "StyleProperty.h"
#include "Invalidation.h"
#include "ThreadAffinity.h"

#include <memory>
#include <map>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace EStudioUI
{
class UiContext;
class CssMotion;
class FontFace;
class Renderer;
namespace Detail
{
class BindingAccess;
}

//! A persistent widget and flex container with explicit child ownership.
class Widget
{
  public:
    //! Creates a panel or a named custom widget type.
    explicit Widget(std::string type = "panel");

    //! Releases children and invalidates non-owning focus/capture handles.
    virtual ~Widget();

    //! Widget addresses remain stable while attached to a tree.
    Widget(const Widget&) = delete;

    //! Widget addresses remain stable while attached to a tree.
    Widget& operator=(const Widget&) = delete;

    //! Throws before unsupported direct access from a thread other than the owner.
    void VerifyAccess() const;

    //! Returns whether the current thread owns this widget.
    bool IsOwnerThread() const noexcept;

    //! Appends an owned child and returns its concrete type.
    template <typename T, typename... Args> T& Add(Args&&... args)
    {
        VerifyStructuralMutation();
        const auto ownerLease = RetainOwner();
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        auto* result = child.get();
        AddChild(std::move(child));
        return *result;
    }

    //! Appends an owned child, rejecting invalid ownership.
    Widget& AddChild(std::unique_ptr<Widget> child);

    //! Removes an immediate child, including its descendants.
    bool RemoveChild(Widget& child);

    //! Removes all children.
    void ClearChildren();

    //! Returns read-only access to owned children.
    const std::vector<std::unique_ptr<Widget>>& GetChildren() const;

    //! Sets the CSS identifier and invalidates styles.
    void SetId(std::string id);

    //! Returns the CSS identifier.
    const std::string& GetId() const;

    //! Adds a CSS class if it is not already present.
    void AddClass(std::string name);

    //! Removes a CSS class.
    void RemoveClass(const std::string& name);

    //! Enables or disables interaction for this subtree.
    void SetEnabled(bool enabled);

    //! Returns effective enabled state including ancestors.
    bool IsEnabled() const;

    //! Includes or removes this subtree from layout, painting, and focus.
    void SetVisible(bool visible);

    //! Returns effective programmatic/CSS visibility including ancestors.
    bool IsVisible() const;

    //! Sets an explicit width contract; code contracts override CSS dimensions.
    void SetWidth(SizeRule rule);

    //! Sets an explicit height contract; code contracts override CSS dimensions.
    void SetHeight(SizeRule rule);

    //! Selects the container's flow direction independently of its theme.
    void SetLayoutDirection(Axis axis);

    //! Sets a nonnegative logical-pixel distance between children.
    void SetGap(float pixels);

    //! Places a child outside normal flow at absolute window coordinates.
    void SetFloatingBounds(Rect bounds);

    //! Removes absolute placement and returns this widget to normal layout flow.
    void ClearFloatingBounds();

    //! Sets a persistent presentation override; explicit layout contracts still win.
    void SetStyleProperty(StyleProperty property, const StyleValue& value);

    //! Removes one presentation override and returns it to the CSS cascade.
    void ClearStyleProperty(StyleProperty property);

    //! Removes all presentation overrides, including completed style animations.
    void ClearStyleProperties();

    //! Returns this node's immediate owner, or null for a detached/root node.
    Widget* GetParent() const;

    //! Requests a context menu; right-click and the Menu key bubble to listeners.
    Signal<Point>& ContextMenuRequested();

    //! Returns whether this widget currently owns keyboard focus.
    bool IsFocused() const;

    //! Returns whether the pointer is over this widget or a descendant.
    bool IsHovered() const;

    //! Returns whether this widget is visually pressed.
    bool IsPressed() const;

    //! Returns the layout rectangle in logical window coordinates.
    Rect GetBounds() const;

    //! Returns the content rectangle after border and padding.
    Rect GetContentBounds() const;

    //! Returns effective ancestor clipping.
    Rect GetClip() const;

    //! Returns computed CSS values.
    const Style& GetStyle() const;

    //! Returns a fixed part's current presentation, or a shared part's default state.
    const Style& GetPartStyle(std::string_view name) const;

    //! Returns a precomputed shared state; lookup allocates nothing and evaluates no selectors.
    const Style& GetPartStyle(std::string_view name, unsigned state) const;

    //! Returns the attached GUI context, or null when detached.
    UiContext* GetContext() const;

    //! Returns whether the widget participates in keyboard navigation.
    virtual bool IsFocusable() const;

    //! Finds a descendant, including this node, by CSS identifier.
    Widget* FindById(const std::string& id);

  protected:
    friend class UiContext;
    friend class WidgetRef;
    friend class Detail::BindingAccess;

    //! Rejects tree mutation during style, measurement, layout, surfaces, or painting.
    void VerifyStructuralMutation() const;

    //! Retains containing window services across callbacks without retaining this widget.
    std::shared_ptr<void> RetainOwner() const;

    //! Returns intrinsic content size before padding, border, and explicit CSS.
    virtual Point Measure(FontFace& font) const;

    //! Returns this widget's last prepared intrinsic size without invoking its measurement hook.
    Point GetIntrinsicSize() const;

    //! Appends this widget's own drawing commands.
    virtual void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const;

    //! Handles a captured left-button press.
    virtual void OnPointerDown(const PointerEvent& event);

    //! Handles pointer movement during capture.
    virtual void OnPointerMove(const PointerEvent& event);

    //! Handles a captured left-button release.
    virtual void OnPointerUp(const PointerEvent& event);

    //! Handles keyboard press/release and repetition.
    virtual void OnKey(Key key, bool down, bool repeat, Modifiers modifiers);

    //! Handles committed Unicode text from the platform.
    virtual void OnText(std::string_view text);

    //! Resets interaction when keyboard focus changes.
    virtual void OnFocus(bool focused);

    //! Cancels an outstanding capture, for example after window deactivation.
    virtual void OnCaptureLost();

    //! Updates internal child geometry before children are arranged.
    virtual void OnLayout();

    //! Handles wheel motion; returning false lets an ancestor scroll instead.
    virtual bool OnScroll(Point delta, Modifiers modifiers);

    //! Intercepts descendant key commands before normal focus dispatch.
    virtual bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers);

    //! Renders external surfaces before the main UI context begins painting.
    virtual void PrepareFrame(double time, float scaleX, float scaleY);

    //! Returns whether this visible widget requests animation frames.
    virtual bool NeedsContinuousRendering() const;

    //! Records completion ordering after the UI has submitted texture consumers.
    virtual void FrameSubmitted();

    //! Requests style/layout reevaluation and a new frame.
    void Invalidate();

    //! Requests only the derived work described by the supplied impacts.
    void Invalidate(Invalidation flags);

    //! Requests painting without repeating style resolution or layout.
    void RequestRepaint();

    //! Registers a named paint part with at most six varying state bits and optional fixed-part motion.
    void RegisterStylePart(std::string name, unsigned stateMask, std::function<Style(const Style&, unsigned)> defaults,
                           bool animated = false);

    //! Selects a fixed part state; animated parts additionally inherit owner hover/pressed states.
    void SetPartState(std::string_view name, unsigned state);

    //! Updates a pseudo-class flag and requests a frame.
    void SetState(unsigned flag, bool enabled);

  private:
    friend class CssMotion;

    //! Captures the owner before any widget state is initialized.
    ThreadAffinity m_Affinity;

    //! Distinguishes target handles even when allocation addresses are reused.
    const std::uint64_t m_TargetKey;

    //! Attaches or detaches a complete subtree.
    void Attach(UiContext* context, Widget* parent);

    //! Supplies a stable identity for selectors.
    StyleNode m_Node;

    //! Stores the last computed style.
    Style m_Style;

    //! Retains the last CSS presentation beneath explicit property overrides.
    Style m_CascadedStyle;

    //! Marks this node's computed CSS and part palettes for resolution.
    bool m_StyleDirty = true;

    //! Marks a style dependency somewhere in this physical subtree.
    bool m_SubtreeStyleDirty = true;

    //! Forces descendant resolution after inherited presentation or visibility changes.
    bool m_DescendantStyleDirty = true;

    //! Marks this container's allocation path for arrangement.
    bool m_ArrangeDirty = true;

    //! Retains the last intrinsic result independently of available constraints.
    Point m_IntrinsicSize;

    //! Reports whether the intrinsic content result remains reusable.
    bool m_IntrinsicValid = false;

    //! Retains the aggregate measurement before arrangement adjustments.
    Point m_MeasuredSize;

    //! Stores the exact parent constraints used by the aggregate measurement.
    Point m_MeasureConstraints;

    //! Reports whether the aggregate measurement remains reusable.
    bool m_MeasureValid = false;

    //! Preserves overflow diagnostics when arrangement skips this container.
    bool m_Overflowing = false;

    //! Stores typed presentation overrides without modifying the stylesheet.
    std::map<StyleProperty, StyleValue> m_StyleOverrides;

    //! Stores bounded style palettes rather than one widget or animation per collection item.
    struct StylePart
    {
        //! CSS part name.
        std::string m_Name;
        //! Varying part states, excluding inherited owner states.
        unsigned m_StateMask = 0;
        //! Explicit active states for a fixed part.
        unsigned m_State = 0;
        //! Effective active states after owner-state inheritance.
        unsigned m_ActiveState = 0;
        //! Enables a single motion layer for this fixed part.
        bool m_Animated = false;
        //! Computes widget-specific defaults from current owner presentation and part state.
        std::function<Style(const Style&, unsigned)> m_Defaults;
        //! Dense state combinations in palette order.
        std::vector<unsigned> m_States;
        //! Precomputed shared variants for allocation-free painting.
        std::vector<Style> m_Palette;
        //! Matched typed declarations reused when owner presentation changes.
        std::vector<std::function<void(Style&)>> m_Assignments;
        //! Temporary whole-stylesheet animation overrides, shared by all items in each variant.
        std::vector<std::map<StyleProperty, StyleValue>> m_Overrides;

        //! Current animated fixed-part presentation.
        Style m_Presentation;
    };

    //! Converts varying state flags into a dense palette index.
    static std::size_t PartIndex(unsigned mask, unsigned state);

    //! Refreshes compiled part defaults after direct owner presentation changes.
    void RefreshStyleParts();

    //! Owns at most 32 parts; stable indices map to motion layers starting at three.
    std::vector<StylePart> m_StyleParts;

    //! Stores the last layout rectangle.
    Rect m_Bounds;

    //! Stores the ancestor clip intersection.
    Rect m_Clip;

    //! Observes the owner of this widget.
    Widget* m_Parent = nullptr;

    //! Observes the GUI context.
    UiContext* m_Context = nullptr;

    //! Owns children in painter order.
    std::vector<std::unique_ptr<Widget>> m_Children;

    //! Invalidates observed widget references at destruction.
    std::shared_ptr<int> m_Lifetime = std::make_shared<int>(0);

    //! Cooperatively cancels receiver-bound work after weak observation expires.
    std::stop_source m_LifetimeStop;

    //! Stores local enabled state.
    bool m_Enabled = true;

    //! Stores the local visibility contract.
    bool m_Visible = true;

    //! Stores an optional code-level width contract.
    std::optional<SizeRule> m_WidthRule;

    //! Stores an optional code-level height contract.
    std::optional<SizeRule> m_HeightRule;

    //! Stores an optional code-level flow direction.
    std::optional<Axis> m_Direction;

    //! Stores an optional code-level gap.
    std::optional<float> m_Gap;

    //! Stores absolute geometry for an out-of-flow child.
    std::optional<Rect> m_FloatingBounds;

    //! Excludes a popup from ordinary layout, hit testing, and painting.
    bool m_Overlay = false;

    //! Delivers application-owned popup requests.
    Signal<Point> m_ContextMenuRequested;
};

//! Observes a widget without extending its lifetime.
class WidgetRef
{
  public:
    //! Creates an empty observer.
    WidgetRef() = default;

    //! Observes a widget with an expiring lifetime token.
    explicit WidgetRef(Widget* widget);

    //! Returns null after destruction; direct access remains restricted to the copied owner thread.
    Widget* Get() const;

  private:
    friend class Widget;

    //! Stores the observed address.
    Widget* m_Widget = nullptr;

    //! Detects destruction even if another widget reuses the same address.
    std::weak_ptr<int> m_Lifetime;

    //! Allows rejection of cross-thread dereference without touching the observed widget.
    std::thread::id m_OwnerThread;
};
} // namespace EStudioUI
