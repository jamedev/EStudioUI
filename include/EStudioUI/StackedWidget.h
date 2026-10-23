#pragma once

#include "Widget.h"

namespace EStudioUI
{
//! Owns persistent pages and lays out only the selected page, without a tab strip.
class StackedWidget final : public Widget
{
  public:
    //! Creates an empty page stack.
    StackedWidget();
    //! Appends a fill-sized container, selecting the first page automatically.
    Widget& AddPage();
    //! Adopts a custom page, preserving its explicit size contracts.
    Widget& AddPage(std::unique_ptr<Widget> page);

    //! Constructs and adopts a custom page, returning its concrete type.
    template <typename T, typename... Args> T& AddPage(Args&&... args)
    {
        VerifyStructuralMutation();
        auto page = std::make_unique<T>(std::forward<Args>(args)...);
        return static_cast<T&>(AddPage(std::move(page)));
    }

    //! Deletes a page and selects the nearest survivor when removing the current page.
    void RemovePage(int index);
    //! Deletes all pages and publishes one empty selection.
    void ClearPages();
    //! Returns a page or throws without changing the selection.
    Widget& GetPage(int index) const;
    //! Returns the number of persistent pages.
    int GetPageCount() const;
    //! Returns the selected page, or null for an empty stack.
    Widget* GetCurrentPage() const;
    //! Selects an existing page by index; invalid indices throw.
    void SetCurrentIndex(int index);
    //! Selects an owned page by identity; unrelated widgets throw.
    void SetCurrentPage(Widget& page);
    //! Returns the selected index, or -1 when empty.
    int GetCurrentIndex() const;
    //! Reports a changed index or page identity after visibility and ownership have committed.
    Signal<int>& CurrentChanged();

  private:
    //! Keeps page ownership and visibility changes on the stack's public boundary.
    using Widget::Add;
    //! Keeps adoption on the page-specific boundary.
    using Widget::AddChild;
    //! Keeps removal on the page-specific boundary.
    using Widget::RemoveChild;
    //! Keeps clearing on the page-specific boundary.
    using Widget::ClearChildren;
    //! Applies committed visibility before observers run.
    void UpdatePages();
    //! Delivers bounded reentrant notifications, including same-index page replacement.
    void PublishCurrent();
    //! Stable, non-owning identities of the children owned by Widget.
    std::vector<WidgetRef> m_Pages;
    //! Committed page index.
    int m_Current = -1;
    //! Monotonic selection/index change revision.
    std::uint64_t m_Revision = 0;
    //! Last revision delivered to observers.
    std::uint64_t m_NotifiedRevision = 0;
    //! Prevents recursive signal delivery.
    bool m_Notifying = false;
    //! Canonical page-selection notification.
    Signal<int> m_CurrentChanged;
};
} // namespace EStudioUI
