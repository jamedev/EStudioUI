#pragma once

#include "MenuBar.h"
#include "Window.h"

namespace EStudioUI
{
//! Hosts menus and arbitrary UI controls in an extended native title bar.
class WindowTitleBar final : public Widget
{
  public:
    //! Creates native-caption command presentations for one containing window.
    explicit WindowTitleBar(Window& window);

    //! Returns the ordinary menu bar in the title row.
    MenuBar& Menus() const;

    //! Returns the flexible title-row container for search, labels and custom widgets.
    Widget& Content() const;

    //! Sets the caption presentation independently of custom title-row widgets.
    void SetCaption(std::string caption);

    //! Resolves a prepared window point without polling events, callbacks or layout.
    WindowHitRegion HitTest(Point windowPoint) const;

  private:
    //! Observes the owned menu bar.
    MenuBar* m_Menus = nullptr;

    //! Observes the owned flexible widget area.
    Widget* m_Content = nullptr;

    //! Observes the owned caption label.
    Label* m_Caption = nullptr;

    //! Observes the native minimize presentation.
    ToolButton* m_Minimize = nullptr;

    //! Observes the native maximize/restore presentation.
    ToolButton* m_Maximize = nullptr;

    //! Observes the native close presentation.
    ToolButton* m_Close = nullptr;
};
} // namespace EStudioUI
