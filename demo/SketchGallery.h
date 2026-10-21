#pragma once

#include <EStudioUI/Label.h>
#include <EStudioUI/SketchWidget.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/Window.h>

#include <utility>

namespace EStudioUI
{
//! A CAD sketch editing playground and a reproducible 200,000-item workload.
class SketchGallery
{
  public:
    //! Builds the toolbars, world-anchored editor, and sample sketch.
    SketchGallery(Window& window, Widget& page);

    //! Disconnects viewport notifications before gallery state is destroyed.
    ~SketchGallery();

    //! Exercises real input and records a native renderer framebuffer capture.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Restores an editable plate sketch without retaining stress-test history.
    void ResetSample();

    //! Builds 200,000 lightweight items and keeps a local neighborhood visible.
    void LoadStress();

    //! Saves the document to the user-editable path and reports I/O errors.
    void SaveDocument();

    //! Atomically loads a versioned sketch from the user-editable path.
    void OpenDocument();

    //! Updates selection, tool, model-size, and zoom readouts.
    void UpdateStatus();

    //! Moves keyboard focus back to the canvas after a toolbar command.
    void FocusCanvas();

    //! Observes the containing native window.
    Window& m_Window;

    //! Observes the canvas owned by the gallery page.
    SketchWidget* m_Sketch = nullptr;

    //! Observes the readout below the canvas.
    Label* m_Status = nullptr;

    //! Observes the editable native sketch file path.
    TextBox* m_FilePath = nullptr;

    //! Observes the sparse widget anchored in world coordinates.
    Widget* m_Note = nullptr;

    //! Observes the toolbar's selected-tool indicators.
    std::vector<std::pair<SketchTool, ToolButton*>> m_Tools;

    //! Disconnects sketch notifications before gallery teardown.
    Connection m_EditConnection;

    //! Disconnects camera notifications before gallery teardown.
    Connection m_ViewConnection;

    //! Stores the last explicitly requested bulk-load duration.
    double m_LoadMilliseconds = 0;

    //! Reports the most recent explicit save/open operation.
    std::string m_FileStatus;
};
} // namespace EStudioUI
