#include <EStudioUI/RadioGroup.h>
#include <EStudioUI/RadioButton.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>
#include <stdexcept>

namespace EStudioUI
{
RadioGroup::RadioGroup() = default;

void RadioGroup::SetSelectionRequired(bool required)
{
    m_Affinity.VerifyAccess("RadioGroup::SetSelectionRequired");
    for (const auto& ref : m_Buttons)
        if (auto* live = ref.Get())
            live->VerifyStructuralMutation();
    m_Required = required;
}

bool RadioGroup::IsSelectionRequired() const
{
    m_Affinity.VerifyAccess("RadioGroup::IsSelectionRequired");
    return m_Required;
}

RadioButton* RadioGroup::GetCheckedButton() const
{
    m_Affinity.VerifyAccess("RadioGroup::GetCheckedButton");
    auto* b = dynamic_cast<RadioButton*>(m_Selected.Get());
    return b && b->m_Group.get() == this ? b : nullptr;
}

Signal<WidgetRef>& RadioGroup::SelectionChanged()
{
    m_Affinity.VerifyAccess("RadioGroup::SelectionChanged");
    return m_SelectionChanged;
}

void RadioGroup::Add(RadioButton& button)
{
    m_Affinity.VerifyAccess("RadioGroup::Add");
    std::erase_if(m_Buttons, [](const WidgetRef& ref) { return !ref.Get(); });
    if (m_Buttons.size() >= 4096)
        throw std::length_error("Radio groups support at most 4,096 members.");
    m_Buttons.emplace_back(&button);
}

void RadioGroup::Remove(RadioButton& button)
{
    m_Affinity.VerifyAccess("RadioGroup::Remove");
    std::erase_if(m_Buttons, [&](const WidgetRef& ref) { return !ref.Get() || ref.Get() == &button; });
    if (m_Selected.Get() == &button)
        m_Selected = {};
}

void RadioGroup::Select(RadioButton* button)
{
    m_Affinity.VerifyAccess("RadioGroup::Select");
    const auto keepAlive = shared_from_this();
    if (button && button->m_Group.get() != this)
        throw std::invalid_argument("Radio selection requires a member of this group.");
    if (!button && m_Required && GetCheckedButton())
        return;
    for (const auto& ref : m_Buttons)
        if (auto* live = ref.Get())
            live->VerifyStructuralMutation();
    m_Queued = WidgetRef(button);
    m_Pending = true;
    if (m_Changing)
        return;
    m_Changing = true;
    try
    {
        unsigned turns = 0;
        while (m_Pending)
        {
            if (++turns > 64)
                throw std::logic_error("Radio observers exceeded 64 reentrant group changes.");
            const auto target = m_Queued;
            m_Pending = false;
            auto* selected = dynamic_cast<RadioButton*>(target.Get());
            if (selected && selected->m_Group.get() != this)
                continue;
            if (m_Selected.Get() == selected)
                continue;
            std::vector<WidgetRef> changed;
            for (const auto& ref : m_Buttons)
                if (auto* live = dynamic_cast<RadioButton*>(ref.Get()); live && live->m_Group.get() == this)
                {
                    const auto state = live == selected ? CheckState::Checked : CheckState::Unchecked;
                    if (live->GetCheckState() != state)
                    {
                        live->ApplyCheckState(state, false);
                        changed.push_back(ref);
                    }
                }
            m_Selected = target;
            for (const auto& ref : changed)
                if (auto* live = dynamic_cast<RadioButton*>(ref.Get()); live && live->m_Group.get() == this)
                    live->NotifyCheckState();
            m_SelectionChanged.Emit(m_Selected);
        }
        m_Changing = false;
    }
    catch (...)
    {
        m_Changing = false;
        m_Pending = false;
        throw;
    }
}

void RadioGroup::Move(RadioButton& origin, bool backwards)
{
    const auto keepAlive = shared_from_this();
    const auto found =
        std::find_if(m_Buttons.begin(), m_Buttons.end(), [&](const WidgetRef& ref) { return ref.Get() == &origin; });
    if (found == m_Buttons.end())
        return;
    const int count = static_cast<int>(m_Buttons.size()), start = static_cast<int>(found - m_Buttons.begin());
    for (int step = 1; step <= count; ++step)
    {
        const auto ref = m_Buttons[static_cast<std::size_t>((start + (backwards ? -step : step) + count) % count)];
        if (auto* live = dynamic_cast<RadioButton*>(ref.Get()); live && live->IsEnabled() && live->IsVisible())
        {
            Select(live);
            if (auto* target = ref.Get(); target && target->GetContext())
                target->GetContext()->SetFocus(target);
            return;
        }
    }
}
} // namespace EStudioUI
