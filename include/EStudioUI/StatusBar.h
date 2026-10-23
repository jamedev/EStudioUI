#pragma once

#include "Label.h"

namespace EStudioUI
{
//! A lightweight status row with a message and persistent application controls.
class StatusBar final : public Widget
{
  public:
    //! Creates an initially empty content-sized status row.
    StatusBar();

    //! Replaces the status message without a timer or implicit expiry.
    void SetMessage(std::string message);

    //! Returns the current message.
    const std::string& GetMessage() const;

    //! Clears the transient message while retaining permanent controls.
    void ClearMessage();

    //! Returns the normal horizontal container for coordinates, progress and custom indicators.
    Widget& Permanent() const;

  private:
    //! Observes the owned flexible message label.
    Label* m_Message = nullptr;

    //! Observes the owned persistent control row.
    Widget* m_Permanent = nullptr;
};
} // namespace EStudioUI
