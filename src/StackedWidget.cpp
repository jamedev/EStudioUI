#include <EStudioUI/StackedWidget.h>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace EStudioUI
{
StackedWidget::StackedWidget() : Widget("stackedwidget")
{
    SetGap(0);
}

Widget& StackedWidget::AddPage()
{
    VerifyStructuralMutation();
    auto page = std::make_unique<Widget>("stackpage");
    page->SetWidth(SizeRule::Fill());
    page->SetHeight(SizeRule::Fill());
    return AddPage(std::move(page));
}

Widget& StackedWidget::AddPage(std::unique_ptr<Widget> page)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (!page)
        throw std::invalid_argument("A stack page must be non-null.");
    page->VerifyAccess();
    if (m_Pages.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::length_error("Stack page count exceeds the supported index range.");
    page->AddClass("stack-page");
    page->SetVisible(m_Pages.empty());
    const WidgetRef added(page.get());
    if (m_Pages.size() == m_Pages.capacity())
        m_Pages.reserve(std::max<std::size_t>(8, m_Pages.capacity() * 2));
    Widget::AddChild(std::move(page));
    m_Pages.push_back(added);
    if (m_Current < 0)
    {
        m_Current = 0;
        ++m_Revision;
        PublishCurrent();
    }
    if (auto* live = added.Get())
        return *live;
    throw std::logic_error("A CurrentChanged observer removed the newly added stack page.");
}

void StackedWidget::RemovePage(int index)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    const WidgetRef removed(&GetPage(index));
    const WidgetRef self(this);
    const int previous = m_Current;
    m_Pages.erase(m_Pages.begin() + index);
    m_Current = m_Pages.empty() ? -1 : std::min(previous - (index < previous ? 1 : 0), GetPageCount() - 1);
    if (index <= previous)
        ++m_Revision;
    UpdatePages();
    Widget::RemoveChild(*removed.Get());
    if (self.Get())
        PublishCurrent();
}

void StackedWidget::ClearPages()
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (m_Pages.empty())
        return;
    const WidgetRef self(this);
    m_Pages.clear();
    m_Current = -1;
    ++m_Revision;
    Widget::ClearChildren();
    if (self.Get())
        PublishCurrent();
}

Widget& StackedWidget::GetPage(int index) const
{
    VerifyAccess();
    if (index < 0 || index >= GetPageCount())
        throw std::out_of_range("Invalid stack page index.");
    if (auto* page = m_Pages[static_cast<std::size_t>(index)].Get())
        return *page;
    throw std::logic_error("Stack pages must be removed with RemovePage or ClearPages.");
}

int StackedWidget::GetPageCount() const
{
    VerifyAccess();
    return static_cast<int>(m_Pages.size());
}

Widget* StackedWidget::GetCurrentPage() const
{
    VerifyAccess();
    return m_Current < 0 ? nullptr : &GetPage(m_Current);
}

void StackedWidget::SetCurrentIndex(int index)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    (void)GetPage(index);
    if (m_Current == index)
        return;
    m_Current = index;
    ++m_Revision;
    UpdatePages();
    PublishCurrent();
}

void StackedWidget::SetCurrentPage(Widget& page)
{
    VerifyStructuralMutation();
    page.VerifyAccess();
    for (std::size_t index = 0; index < m_Pages.size(); ++index)
        if (m_Pages[index].Get() == &page)
        {
            SetCurrentIndex(static_cast<int>(index));
            return;
        }
    throw std::invalid_argument("The selected page must belong to this stack.");
}

void StackedWidget::UpdatePages()
{
    for (std::size_t index = 0; index < m_Pages.size(); ++index)
        if (auto* page = m_Pages[index].Get())
            page->SetVisible(static_cast<int>(index) == m_Current);
    Invalidate(Invalidation::Content);
    NotifySemanticsChanged();
}

void StackedWidget::PublishCurrent()
{
    if (m_Notifying)
        return;
    const WidgetRef self(this);
    m_Notifying = true;
    try
    {
        unsigned turns = 0;
        while (m_NotifiedRevision != m_Revision)
        {
            if (++turns > 64)
                throw std::logic_error("Stack selection observers exceeded 64 reentrant changes.");
            m_NotifiedRevision = m_Revision;
            m_CurrentChanged.Emit(m_Current);
            if (!self.Get())
                return;
        }
        m_Notifying = false;
    }
    catch (...)
    {
        if (self.Get())
            m_Notifying = false;
        throw;
    }
}

int StackedWidget::GetCurrentIndex() const
{
    VerifyAccess();
    return m_Current;
}

Signal<int>& StackedWidget::CurrentChanged()
{
    VerifyAccess();
    return m_CurrentChanged;
}
} // namespace EStudioUI
