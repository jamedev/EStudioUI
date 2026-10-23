#pragma once

#include "Button.h"

namespace EStudioUI
{
//! Stable standard response values carried by PopupResult::m_Value.
enum class StandardButton
{
    None = 0,
    Ok,
    Cancel,
    Yes,
    No,
    Apply,
    Close,
    Save,
    Discard,
    Reset,
    Help
};

//! Determines whether a standard response accepts, rejects, or leaves its dialog open.
enum class DialogButtonRole
{
    Accept,
    Reject,
    Destructive,
    Apply,
    Reset,
    Help
};

//! Composes a bounded set of ordinary CSS buttons with stable response identities.
class DialogButtonBox final : public Widget
{
  public:
    //! Creates an empty, horizontally arranged response row.
    DialogButtonBox();

    //! Replaces up to ten unique standard buttons after validating the complete candidate.
    void SetButtons(std::vector<StandardButton> buttons);

    //! Returns the surviving presentation of a standard response, or null.
    Button* GetButton(StandardButton button) const;

    //! Returns the configured response order independently of translated/customized captions.
    const std::vector<StandardButton>& GetButtons() const;

    //! Returns the canonical role, rejecting None or an unknown response.
    static DialogButtonRole Role(StandardButton button);

    //! Emits the selected stable response; ordinary button customization remains available.
    Signal<StandardButton>& Clicked();

  private:
    //! Stores the configured, validated order.
    std::vector<StandardButton> m_Buttons;
    //! Observes each corresponding owned button.
    std::vector<WidgetRef> m_Presentations;
    //! Delivers response activation after the button's ordinary interaction completes.
    Signal<StandardButton> m_Clicked;
};
} // namespace EStudioUI
