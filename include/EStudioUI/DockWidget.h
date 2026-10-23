#pragma once

#include "Widget.h"

namespace EStudioUI
{
class DockManager;
class Label;
class ToolButton;

//! One persistent tool panel whose actual content survives docking and floating.
class DockWidget final : public Widget
{
  public:
    //! Creates a panel; normal applications use DockManager::AddDock.
    DockWidget(std::string id, std::string title);

    //! Returns the stable workspace identity, independent of tab order and title.
    const std::string& GetDockId() const;

    //! Returns the caption.
    const std::string& GetTitle() const;

    //! Changes the caption in both docked and floating presentations.
    void SetTitle(std::string title);

    //! Returns the ordinary container for application widgets and compositions.
    Widget& Content() const;

    //! Returns whether this panel currently occupies a separate native window.
    bool IsFloating() const;

    //! Returns whether the panel is present in the workspace, including inactive tabs.
    bool IsOpen() const;

    //! Hides this panel without destroying its content.
    void Close();

  private:
    friend class DockManager;

    //! Observes the manager while its widget lifetime remains valid.
    WidgetRef m_Manager;

    //! Stable printable ASCII identity used in workspace snapshots.
    std::string m_DockId;

    //! Caption independent of stable identity.
    std::string m_Title;

    //! Observes the owned caption label.
    Label* m_Caption = nullptr;

    //! Observes the owned dock/float command.
    ToolButton* m_Float = nullptr;

    //! Observes the owned application content.
    Widget* m_Content = nullptr;
};
} // namespace EStudioUI
