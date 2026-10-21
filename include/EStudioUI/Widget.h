#pragma once

#include "Signal.h"
#include "Style.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace EStudioUI
{
class UiContext;
class FontFace;
class Renderer;

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

    //! Appends an owned child and returns its concrete type.
    template <typename T, typename... Args> T& Add(Args&&... args)
    {
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

    //! Returns the attached GUI context, or null when detached.
    UiContext* GetContext() const;

    //! Returns whether the widget participates in keyboard navigation.
    virtual bool IsFocusable() const;

    //! Finds a descendant, including this node, by CSS identifier.
    Widget* FindById(const std::string& id);

  protected:
    friend class UiContext;
    friend class WidgetRef;

    //! Returns intrinsic content size before padding, border, and explicit CSS.
    virtual Point Measure(FontFace& font) const;

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

    //! Requests style/layout reevaluation and a new frame.
    void Invalidate();

    //! Requests painting without repeating style resolution or layout.
    void RequestRepaint();

    //! Updates a pseudo-class flag and requests a frame.
    void SetState(unsigned flag, bool enabled);

  private:
    //! Attaches or detaches a complete subtree.
    void Attach(UiContext* context, Widget* parent);

    //! Supplies a stable identity for selectors.
    StyleNode m_Node;

    //! Stores the last computed style.
    Style m_Style;

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

    //! Stores local enabled state.
    bool m_Enabled = true;
};

//! Observes a widget without extending its lifetime.
class WidgetRef
{
  public:
    //! Creates an empty observer.
    WidgetRef() = default;

    //! Observes a widget with an expiring lifetime token.
    explicit WidgetRef(Widget* widget);

    //! Returns null after the observed widget has been destroyed.
    Widget* Get() const;

  private:
    friend class Widget;

    //! Stores the observed address.
    Widget* m_Widget = nullptr;

    //! Detects destruction even if another widget reuses the same address.
    std::weak_ptr<int> m_Lifetime;
};
} // namespace EStudioUI
