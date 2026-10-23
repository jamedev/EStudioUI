#include <EStudioUI/DialogButtonBox.h>

#include <array>
#include <stdexcept>

namespace EStudioUI
{
DialogButtonBox::DialogButtonBox() : Widget("dialogbuttonbox")
{
    SetLayoutDirection(Axis::Horizontal);
    SetJustification(LayoutJustification::End);
    SetGap(8);
    SetHeight(SizeRule::Content());
}

DialogButtonRole DialogButtonBox::Role(StandardButton button)
{
    switch (button)
    {
    case StandardButton::Ok:
    case StandardButton::Yes:
    case StandardButton::Save:
        return DialogButtonRole::Accept;
    case StandardButton::Cancel:
    case StandardButton::No:
    case StandardButton::Close:
        return DialogButtonRole::Reject;
    case StandardButton::Discard:
        return DialogButtonRole::Destructive;
    case StandardButton::Apply:
        return DialogButtonRole::Apply;
    case StandardButton::Reset:
        return DialogButtonRole::Reset;
    case StandardButton::Help:
        return DialogButtonRole::Help;
    default:
        throw std::invalid_argument("Unknown standard dialog response.");
    }
}

void DialogButtonBox::SetButtons(std::vector<StandardButton> buttons)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    std::array<bool, 11> present{};
    if (buttons.size() > 10)
        throw std::length_error("A standard dialog button box supports at most ten responses.");
    for (const auto button : buttons)
    {
        (void)Role(button);
        if (std::exchange(present[static_cast<std::size_t>(button)], true))
            throw std::invalid_argument("A standard dialog response may only occur once.");
    }
    if (m_Buttons == buttons && m_Presentations.size() == buttons.size() &&
        std::all_of(m_Presentations.begin(), m_Presentations.end(), [](const auto& item) { return item.Get(); }))
        return;
    ClearChildren();
    m_Buttons = std::move(buttons);
    m_Presentations.clear();
    static constexpr const char* captions[]{"",      "OK",   "Cancel",  "Yes",   "No",  "Apply",
                                            "Close", "Save", "Discard", "Reset", "Help"};
    static constexpr const char* classes[]{"dialog-accept", "dialog-reject", "dialog-destructive",
                                           "dialog-apply",  "dialog-reset",  "dialog-help"};
    const WidgetRef self(this);
    for (const auto value : m_Buttons)
    {
        auto& button = Add<Button>(captions[static_cast<std::size_t>(value)]);
        button.SetWidth(SizeRule::Content(72));
        button.AddClass(classes[static_cast<std::size_t>(Role(value))]);
        m_Presentations.emplace_back(&button);
        button.Clicked().Connect(
            [self, value]
            {
                if (auto* live = dynamic_cast<DialogButtonBox*>(self.Get()))
                {
                    const auto ownerLease = live->RetainOwner();
                    live->m_Clicked.Emit(value);
                }
            });
    }
}

Button* DialogButtonBox::GetButton(StandardButton button) const
{
    VerifyAccess();
    const auto found = std::find(m_Buttons.begin(), m_Buttons.end(), button);
    return found == m_Buttons.end()
               ? nullptr
               : dynamic_cast<Button*>(m_Presentations[static_cast<std::size_t>(found - m_Buttons.begin())].Get());
}

const std::vector<StandardButton>& DialogButtonBox::GetButtons() const
{
    VerifyAccess();
    return m_Buttons;
}

Signal<StandardButton>& DialogButtonBox::Clicked()
{
    VerifyAccess();
    return m_Clicked;
}
} // namespace EStudioUI
