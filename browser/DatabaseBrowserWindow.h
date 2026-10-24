#pragma once

#include <EStudioUI/AdapterModel.h>
#include <EStudioUI/Application.h>
#include <EStudioUI/MainWindow.h>
#include <EStudioUI/TreeTable.h>
#include <EStudioUI/TabWidget.h>

namespace EStudioUI
{
//! Consumes public adapter/view APIs in a working SQLite browser and custom-model playground.
class DatabaseBrowserWindow
{
  public:
    //! Builds the shell without reading a database on the GUI thread.
    DatabaseBrowserWindow(Application& application, Window& window);

    //! Expires application callbacks before views and background targets are released.

    ~DatabaseBrowserWindow();

    //! Starts an existing database in read-only mode unless editing was explicitly requested.
    void Open(std::filesystem::path path, bool writable = false);

    //! Creates/opens the engineering sample on an application worker.
    void OpenExample(std::filesystem::path path, std::size_t rows = 100000);

    //! Returns the current asynchronous source, or null before opening a file.
    std::shared_ptr<AdapterModel> GetModel() const;

    //! Returns the actual result-window table.
    TableWidget& DataView() const;

    //! Returns the hierarchy/schema inspector.
    TreeTable& SchemaView() const;

    //! Returns the shared in-memory hierarchy used by the adapter-extension example.
    TreeTable& Playground() const;

    //! Returns the root navigation tabs for renderer acceptance.
    TabWidget& Tabs() const;

  private:
    //! Retains model/control lifetime metadata across async callbacks.
    struct Data;

    //! Owns app-side policy without changing source snapshot ownership.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
