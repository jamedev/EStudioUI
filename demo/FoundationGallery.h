#pragma once

#include <EStudioUI/BackgroundTasks.h>
#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/Window.h>

#include <atomic>

namespace EStudioUI
{
//! Demonstrates cancellable background scene generation and owner-side publication.
class FoundationGallery
{
  public:
    //! Builds a working async example using the public task and handle APIs.
    FoundationGallery(Window& window, Widget& page, std::filesystem::path assets);

    //! Cancels pending deliveries before owned windows and worker resources are released.
    ~FoundationGallery();

    //! Verifies completion, replacement, target deletion, cancellation, and retained idle rendering.
    void Verify(const std::filesystem::path& output);

  private:
    //! Starts the next generation, replacing pending work for the previous request.
    void Start(std::size_t count);

    //! Cancels the current request without waiting for its worker.
    void Cancel();

    //! Removes the receiving canvas while a worker may still be computing.
    void DeleteTarget();

    //! Creates a fresh receiving canvas with an independent target lifetime.
    void CreateTarget();

    //! Opens another registered window without starting a nested event loop.
    void OpenWindow();

    //! Pumps owner-side delivery until the latest task settles, with a finite test deadline.
    void Await();

    //! Observes the containing native window.
    Window& m_Window;

    //! Observes the gallery page owned by the tab tree.
    Widget& m_Page;

    //! Stores installed asset paths for additional windows.
    std::filesystem::path m_Assets;

    //! Owns the bounded worker executor.
    BackgroundTasks m_Tasks;

    //! Makes older scene requests obsolete automatically.
    RequestScope m_Requests;

    //! Observes the latest computation and delivery outcome.
    Task m_Task;

    //! Observes the surface owner used for target replacement.
    Widget* m_Surface = nullptr;

    //! Observes the currently live receiving canvas.
    CanvasWidget* m_Canvas = nullptr;

    //! Observes the progress and outcome caption.
    Label* m_Status = nullptr;

    //! Counts successful target deliveries independently of gallery callback captures.
    std::shared_ptr<std::atomic<int>> m_Completions = std::make_shared<std::atomic<int>>(0);

    //! Records request identity for human-readable progress.
    unsigned m_Generation = 0;

    //! Owns additional native windows registered with the common application loop.
    std::vector<std::unique_ptr<Window>> m_Windows;
};
} // namespace EStudioUI
