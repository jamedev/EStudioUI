#pragma once
#include "ValidationMessage.h"
#include "UiTimer.h"

namespace EStudioUI
{
//! Bounded nonmodal notifications anchored to a viewport corner without replacing popup sessions.
class NotificationCenter : public Widget
{
  public:
    //! Creates an empty notification overlay; prefer Create.
    NotificationCenter();
    //! Adds a bottom-right root-owned overlay to an attached UI context.
    static NotificationCenter& Create(UiContext& context);
    //! Shows a message, evicting the oldest of four visible entries; zero timeout is persistent.
    std::uint64_t Show(std::string text, FeedbackSeverity severity = FeedbackSeverity::Information, double seconds = 5);
    //! Removes one notification by its stable identity.
    bool Dismiss(std::uint64_t id);
    //! Removes all notifications and cancels their deadlines.
    void Clear();
    //! Returns the current bounded visible count.
    std::size_t GetCount() const;

  protected:
    //! Exposes one bounded polite live region for the visible message set.
    SemanticInfo DescribeSemantics() const override;

  private:
    //! One visible message and its timer.
    struct Entry
    {
        //! Stable notification ID.
        std::uint64_t m_Id = 0;
        //! Root-owned row observed during removal.
        WidgetRef m_Row;
        //! Expiry registration.
        UiTimer m_Timer;
    };

    //! At most four visible notifications.
    std::vector<Entry> m_Entries;
};
} // namespace EStudioUI
