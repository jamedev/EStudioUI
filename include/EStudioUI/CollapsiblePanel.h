#pragma once

#include "ToolButton.h"

namespace EStudioUI
{
//! A keyboard-operable inspector section retaining its content while collapsed.
class CollapsiblePanel : public Widget
{
  public:
    //! Creates an expanded section with an ordinary content container.
    explicit CollapsiblePanel(std::string title = {});

    //! Returns the persistent child container.
    Widget& Content() const;

    //! Updates the visible and accessible header.
    void SetTitle(std::string title);

    //! Returns the header caption.
    const std::string& GetTitle() const;

    //! Hides or reveals content without destroying values, bindings or children.
    void SetExpanded(bool expanded);

    //! Returns configured expansion independently of hidden ancestors.
    bool IsExpanded() const;

    //! Reports expansion changes after content and CSS state agree.
    Signal<bool>& ExpandedChanged();

  protected:
    //! Advertises canonical group expansion to accessibility adapters.
    SemanticInfo DescribeSemantics() const override;

    //! Expands or collapses through the ordinary canonical setter.
    SemanticStatus OnSemanticAction(const SemanticCommand& command) override;

  private:
    //! Observes the owned keyboard-operable header.
    ToolButton* m_Header = nullptr;

    //! Observes the retained content container.
    Widget* m_Content = nullptr;

    //! Owns the visible title.
    std::string m_Title;

    //! Retains the configured expansion state.
    bool m_Expanded = true;

    //! Reports coherent changes.
    Signal<bool> m_ExpandedChanged;
    //! Bounds synchronous reentrant observer delivery.
    unsigned m_NotificationDepth = 0;
};
} // namespace EStudioUI
