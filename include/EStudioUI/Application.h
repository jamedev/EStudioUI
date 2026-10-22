#pragma once

#include "BackgroundTasks.h"
#include "ThreadAffinity.h"

#include <cstddef>
#include <memory>

namespace EStudioUI
{
class Window;

//! Schedules all open windows through one main-thread native event loop.
class Application
{
  public:
    //! Establishes the application owner on the main thread; one explicit instance may exist.
    Application();

    //! Requests closure of remaining windows without taking their C++ ownership.
    ~Application();

    //! An application has one explicit owner.
    Application(const Application&) = delete;

    //! An application has one explicit owner.
    Application& operator=(const Application&) = delete;

    //! Runs until every registered window closes or Quit is requested; nested loops are rejected.
    int Run();

    //! Polls native events and fairly services all windows once without waiting.
    bool ProcessEvents();

    //! Requests closure of all windows and cancels their pending UI delivery.
    void Quit();

    //! Returns the number of registered windows that have not requested closure.
    std::size_t GetWindowCount() const;

    //! Returns whether the shared application loop is running.
    bool IsRunning() const;

    //! Lazily creates the application's shared background pool for simple task submission.
    BackgroundTasks& Tasks();

    //! Diagnoses unsupported direct use from a different thread.
    void VerifyAccess() const;

  private:
    friend class Window;

    //! Preserves Window::Run convenience while scheduling every registered window.
    static int RunWindows();

    //! Captures the thread that constructed the explicit application.
    ThreadAffinity m_Affinity;

    //! Owns a pool only when Tasks is first requested; shutdown joins its cooperative workers.
    std::unique_ptr<BackgroundTasks> m_Tasks;
};
} // namespace EStudioUI
