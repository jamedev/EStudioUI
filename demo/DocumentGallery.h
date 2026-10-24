#pragma once

#include <EStudioUI/Window.h>

namespace EStudioUI
{
//! Demonstrates real engineering documents and native document-workspace acceptance.
class DocumentGallery
{
  public:
    //! Adds an interactive workspace with native pickers and worker-side JSON persistence.
    DocumentGallery(Window& owner, Widget& page);
    //! Cancels UI delivery before retiring bounded background work.
    ~DocumentGallery();
    //! Exercises actual tab dragging, dirty close decisions, stale saves and native detach.
    void Verify(const std::filesystem::path& assets, const std::filesystem::path& output);

  private:
    //! Retains application models independently of input/picker callback lifetimes.
    struct Data;
    //! Owns sample data, file work and weak widget observers.
    std::shared_ptr<Data> m_Data;
};
} // namespace EStudioUI
