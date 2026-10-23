#pragma once

#include "Dialog.h"

namespace EStudioUI
{
//! Selects semantic feedback styling without requiring raster/icon resources.
enum class MessageKind
{
    Information,
    Question,
    Warning,
    Error
};

//! A reusable asynchronous message/confirmation dialog with owned wrapping text.
class MessageBox final : public Dialog
{
  public:
    //! Creates a hidden informational message with one OK response.
    MessageBox(std::string title = {}, std::string message = {});

    //! Creates a root-owned message following its attached logical owner.
    static MessageBox& Create(Widget& owner, std::string title, std::string message);

    //! Replaces the displayed message and accessible description.
    void SetMessage(std::string message);

    //! Returns the canonical message.
    const std::string& GetMessage() const;

    //! Selects information/question/warning/error CSS classes.
    void SetKind(MessageKind kind);

    //! Returns the current feedback kind.
    MessageKind GetKind() const;

  private:
    //! Owns the wrapping display through Content().
    Label* m_Message = nullptr;
    //! Stores the selected feedback style.
    MessageKind m_Kind = MessageKind::Information;
};
} // namespace EStudioUI
