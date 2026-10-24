#pragma once

#include "DesignerSurface.h"
#include "WidgetPalette.h"
#include <EStudioUI/ThemeController.h>

#include <EStudioUI/Application.h>
#include <EStudioUI/MainWindow.h>
#include <EStudioUI/PropertyGrid.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/ListWidget.h>

namespace EStudioUI
{
//! The first real authoring application consuming ordinary EStudioUI controls and services.
class CreatorWindow
{
  public:
    //! Composes the workspace in an existing window with an explicit base theme.
    CreatorWindow(Application& application, Window& window, std::string baseCss);

    //! Expires callbacks before releasing canonical documents and tasks.
    ~CreatorWindow();

    //! Returns canonical document state for application extension and tests.
    State<UiDocument>& Document();

    //! Returns the extensible widget adapter registry.
    WidgetRegistry& Registry();

    //! Loads a validated design, clears history and marks the document saved.
    void LoadDocument(UiDocument document);

    //! Selects a stable design node and rebuilds only its inspector metadata.
    void SelectNode(std::string id);

    //! Adds an actual registered widget to a selected/explicit container.
    std::string AddWidget(std::string type, std::string parent = {}, std::optional<Point> position = {});

    //! Captures a selected subtree as a reusable session palette composition.
    void RegisterComponent(std::string name, std::string id = {});
    //! Sets edge/center/grid and persistent-relationship authoring preferences.
    void SetSnapping(DesignSnapSettings settings);
    //! Returns current snapping preferences.
    const DesignSnapSettings& GetSnapping() const;
    //! Returns the live application theme controller.
    ThemeController& Themes() const;

    //! Deletes the selected subtree and its direct sibling constraints.
    void DeleteSelected();

    //! Commits one validated typed property through canonical document state.
    void SetProperty(std::string_view id, std::string name, PropertyValue value);

    //! Restores the previous bounded document snapshot.
    void Undo();

    //! Restores a previously undone snapshot.
    void Redo();

    //! Applies complete source CSS atomically after parsing.
    void ApplyCss(std::string css);

    //! Synchronizes queued preview changes at an idle frame boundary.
    void Refresh();

    //! Returns the production preview for framebuffer and interaction checks.
    DesignerSurface& Surface() const;

    //! Returns the active property inspector.
    PropertyGrid& Inspector() const;

    //! Returns whether canonical data differs from the saved document.
    bool IsDirty() const;

    //! Creates a reusable dashboard fixture using real widgets and named constraints.
    static UiDocument ExampleDocument();

  private:
    //! Builds command, palette, outline, style and inspector panels.
    void BuildShell();

    //! Rebuilds stable palette filtering.
    void FilterPalette(std::string_view query);

    //! Rebuilds document outline and editable constraint rows.
    void RefreshOutline();

    //! Builds property metadata for the selected node and layout context.
    void RefreshInspector();

    //! Converts one complete inspector candidate to an owned design.
    UiDocument FromInspector(const std::vector<PropertyValue>& values) const;

    //! Applies captured geometry provisionally and commits once per completed gesture.
    void EditGeometry(std::string id, Point delta, bool resize, bool finished);

    //! Cancels provisional geometry without changing canonical state or history.
    void CancelGeometry();
    //! Builds the appearance settings inspector from ordinary property controls.
    void BuildAppearance();
    //! Opens an asynchronous JSON file picker and reads owned bytes on a worker.
    void Open(bool discardConfirmed = false);

    //! Writes the selected independent JSON or C++ output on a worker.
    void Save(bool cpp, bool selected = false);

    //! Applies edited constraint rows after complete prevalidation.
    void ApplyConstraints();

    //! Publishes a useful status/error message.
    void Status(std::string message);

    //! Owns the application event/task domain by reference.
    Application& m_Application;

    //! Observes the containing native/headless window.
    Window& m_Window;

    //! Owns explicit construction adapters.
    WidgetRegistry m_Registry;

    //! Owns canonical validated document state.
    std::unique_ptr<State<UiDocument>> m_Document;

    //! Owns the current property model.
    std::shared_ptr<PropertyModel> m_Properties;

    //! Retires inspector adapters before replacing metadata.
    std::vector<ScopedConnection> m_InspectorConnections;

    //! Retires controller state/frame subscriptions.
    std::vector<ScopedConnection> m_Connections;

    //! Observes the actual application shell.
    MainWindow* m_Shell = nullptr;

    //! Observes the real preview and authoring overlay.
    DesignerSurface* m_Surface = nullptr;

    //! Observes the property inspector.
    PropertyGrid* m_Inspector = nullptr;

    //! Observes the virtual palette and outline.
    WidgetPalette* m_Palette = nullptr;

    //! Observes the virtual document outline.
    ListWidget* m_Outline = nullptr;

    //! Observes the source CSS line editor.
    TableWidget* m_Css = nullptr;

    //! Observes the editable constraint table.
    TableWidget* m_Constraints = nullptr;

    //! Stable palette and outline identity lookup.
    std::vector<std::string> m_PaletteIds, m_OutlineIds;

    //! Stable selected node identity.
    std::string m_Selected = "design-root";

    //! Stable inspector target identity.
    std::string m_InspectorTarget;

    //! Stable constraint owner identity.
    std::string m_ConstraintOwner;

    //! Applies a base application theme before document-specific source CSS.
    std::string m_BaseCss;

    //! Remembers the CSS last applied to avoid parsing on ordinary edits.
    std::string m_AppliedCss;

    //! Owns live application appearance independently of design state.
    ThemeController* m_Theme = nullptr;
    //! Coalesces interactive appearance editing to the next frame.
    std::optional<Theme> m_PendingTheme;
    //! Owns appearance inspector state.
    std::shared_ptr<PropertyModel> m_AppearanceModel;
    //! Observes the appearance inspector.
    PropertyGrid* m_AppearanceGrid = nullptr;
    //! Owns reusable flattened design templates; instances serialize as ordinary nodes.
    std::map<std::string, UiDocument> m_Components;
    //! Owns geometry authoring preferences.
    DesignSnapSettings m_Snapping;
    //! Captures parent content bounds and sibling geometry once per gesture.
    Rect m_GestureParent;
    //! Owns the bounded sibling snapshot for snapping.
    std::vector<DesignReference> m_GestureReferences;
    //! Observes the provisional geometry target.
    WidgetRef m_GestureTarget;
    //! Suppresses a gesture invalidated by another document publication.
    bool m_AbortGesture = false;

    //! Owns bounded history snapshots.
    std::vector<std::shared_ptr<const UiDocument>> m_Undo, m_Redo;

    //! Owns the last saved design snapshot.
    std::shared_ptr<const UiDocument> m_Saved;

    //! Owns the last previewed immutable design.
    std::shared_ptr<const UiDocument> m_Previewed;

    //! Owns the currently built weak widget index.
    UiBuildResult m_Built;

    //! Stores the initial gesture document to coalesce drag history.
    std::shared_ptr<const UiDocument> m_Gesture;

    //! Captures initial preview geometry.
    Rect m_GestureBounds;

    //! Suppresses history capture while replaying.
    bool m_Replaying = false;

    //! Suppresses inspector feedback during canonical refresh.
    bool m_Syncing = false;

    //! Marks coalesced preview work for the next frame.
    bool m_PreviewDirty = true;

    //! Invalidates stale asynchronous file completions.
    std::uint64_t m_FileGeneration = 0;

    //! Owns the current JSON file identity.
    std::filesystem::path m_Path;

    //! Retains receiver-bound tasks without blocking the UI.
    std::vector<Task> m_Tasks;

    //! Expires non-widget controller captures at destruction.
    std::shared_ptr<void> m_Lifetime = std::make_shared<int>(0);
};
} // namespace EStudioUI
