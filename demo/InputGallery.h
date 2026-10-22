#pragma once

#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Working shared commands, nested menus, focus scopes, and asynchronous modal results.
class InputGallery
{
  public:
    //! Builds the F09 acceptance page using the delivered public API.
    InputGallery(Window& window, Widget& page);

    //! Verifies commands, production native callbacks, stacked GPU painting, and idle reuse.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Opens a modal panel whose result updates a weakly observed status caption.
    void OpenSettings();

    //! Observes the owner window.
    Window& m_Window;

    //! Shares one command across toolbar, standalone button, menu, and shortcut.
    std::shared_ptr<Action> m_Save;

    //! Shares one checkable command across toolbar and nested menu.
    std::shared_ptr<Action> m_Grid;

    //! Observes the canonical command's toolbar presentation.
    WidgetRef m_SaveButton;

    //! Observes the settings launcher.
    WidgetRef m_SettingsButton;

    //! Observes the single-line editor used for focus and shortcut checks.
    WidgetRef m_Editor;

    //! Observes the caption updated by commands and asynchronous results.
    WidgetRef m_Status;

    //! Observes the root command menu.
    WidgetRef m_Menu;

    //! Observes the modal surface assembled from ordinary widgets.
    WidgetRef m_Modal;

    //! Observes the modal surface's first editable field.
    WidgetRef m_ModalEditor;

    //! Observes a chooser owned logically by a button inside the modal surface.
    WidgetRef m_ModalMenu;

    //! Tracks the current modal response without blocking the application loop.
    PopupSession m_Session;

    //! Counts actual command executions, including shortcuts and menu activation.
    int m_Executions = 0;

    //! Disconnects gallery callbacks before the controller is destroyed.
    std::vector<ScopedConnection> m_Connections;
};
} // namespace EStudioUI
