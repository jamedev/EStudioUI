#include <EStudioUI/SegmentedControl.h>
#include <algorithm>
#include <stdexcept>

namespace EStudioUI
{
SegmentedControl::SegmentedControl() : Widget("segmentedcontrol"), m_Group(std::make_shared<RadioGroup>())
{
    SetLayoutDirection(Axis::Horizontal);
    SetGap(0);
    const WidgetRef self(this);
    m_Group->SelectionChanged().Connect(
        [self](WidgetRef)
        {
            if (auto* live = dynamic_cast<SegmentedControl*>(self.Get()); live && !live->m_Rebuilding)
            {
                live->m_CurrentChanged.Emit(live->GetCurrentIndex());
            }
        });
}

void SegmentedControl::SetItems(std::vector<std::string> items)
{
    VerifyStructuralMutation();
    if (items.size() > 64 ||
        std::any_of(items.begin(), items.end(), [](const auto& item) { return item.size() > 4096; }))
    {
        throw std::length_error("Segmented controls accept 64 captions of at most 4096 bytes.");
    }
    m_Rebuilding = true;
    while (!m_Buttons.empty())
    {
        auto* button = m_Buttons.back();
        m_Buttons.pop_back();
        RemoveChild(*button);
    }
    for (auto& caption : items)
    {
        auto& button = Add<RadioButton>(std::move(caption), m_Group);
        button.AddClass("segment");
        button.SetWidth(SizeRule::Fill());
        m_Buttons.push_back(&button);
    }
    if (!m_Buttons.empty())
    {
        m_Group->Select(m_Buttons.front());
    }
    m_Rebuilding = false;
    m_CurrentChanged.Emit(GetCurrentIndex());
}

void SegmentedControl::SetCurrentIndex(int index)
{
    VerifyStructuralMutation();
    if (index < -1 || index >= static_cast<int>(m_Buttons.size()) || (index == -1 && !m_Buttons.empty()))
    {
        throw std::out_of_range("Invalid segment index.");
    }
    m_Group->Select(index < 0 ? nullptr : m_Buttons[static_cast<std::size_t>(index)]);
}

int SegmentedControl::GetCurrentIndex() const
{
    VerifyAccess();
    const auto found = std::find(m_Buttons.begin(), m_Buttons.end(), m_Group->GetCheckedButton());
    return found == m_Buttons.end() ? -1 : static_cast<int>(found - m_Buttons.begin());
}

Signal<int>& SegmentedControl::CurrentChanged()
{
    VerifyAccess();
    return m_CurrentChanged;
}
} // namespace EStudioUI
