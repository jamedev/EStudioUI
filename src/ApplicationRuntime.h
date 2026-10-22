#pragma once

#include <functional>
#include <memory>

namespace EStudioUI::Detail
{
//! Observes native window behavior without depending on Window's C++ lifetime.
struct ApplicationWindow
{
    //! Reports whether this registration still represents an open window.
    std::function<bool()> m_IsOpen;

    //! Delivers bounded UI work and returns the next native-clock deadline, or -1.
    std::function<double()> m_Service;

    //! Requests closure while preserving callback stack lifetime.
    std::function<void()> m_Close;
};

//! Checks the process-wide native runtime owner before touching GLFW state.
void VerifyApplicationAccess();

//! Adds a weak registration so ordinary Window ownership remains unchanged.
void RegisterApplicationWindow(const std::shared_ptr<ApplicationWindow>& window);

//! Retains native resources until the current callback or application traversal finishes.
void RetireApplicationWindow(std::shared_ptr<void> window);

//! Releases deferred resources only outside native callbacks and scheduler traversal.
void FlushRetiredWindows();

//! Allows native presentation only at an outermost callback, outside application-owned work.
bool CanServiceNativeFrame();

//! Services registered windows without recursively polling native events during a system modal loop.
double ServiceNativeWindows();

//! Marks a native or user callback region where destroying a GLFW window is deferred.
class ApplicationDispatchScope
{
  public:
    //! Enters a non-destruction region on the runtime owner.
    ApplicationDispatchScope();

    //! Leaves the region without destroying resources inside the callback stack.
    ~ApplicationDispatchScope();

    //! Callback scopes cannot duplicate their depth ownership.
    ApplicationDispatchScope(const ApplicationDispatchScope&) = delete;

    //! Callback scopes cannot duplicate their depth ownership.
    ApplicationDispatchScope& operator=(const ApplicationDispatchScope&) = delete;
};
} // namespace EStudioUI::Detail
