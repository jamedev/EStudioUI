#include "PropertyNotification.h"
#include <EStudioUI/UnitEditor.h>

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
UnitEditor::UnitEditor(double value) : Widget("uniteditor")
{
    SetLayoutDirection(Axis::Horizontal);
    SetGap(4);
    m_Editor = &Add<NumericInput>();
    m_Editor->SetWidth(SizeRule::Fill(1, 60));
    m_Unit = &Add<ComboBox>();
    m_Unit->AddClass("unit-choice");
    m_Unit->SetWidth(SizeRule::Fixed(76));
    m_Unit->SetVisible(false);
    SetBaselineChild(m_Editor);
    const WidgetRef self(this);
    m_Editor->ValueChanged().Connect(
        [self](double displayed)
        {
            auto* live = dynamic_cast<UnitEditor*>(self.Get());
            if (!live || live->m_Syncing)
            {
                return;
            }
            const auto unit = live->m_Units.empty() ? UnitDefinition{} : live->m_Units[live->m_Current];
            const double canonical = displayed * unit.m_Scale + unit.m_Offset;
            live->SetValue(std::clamp(canonical, live->m_Minimum, live->m_Maximum));
        });
    m_Unit->CurrentChanged().Connect(
        [self](int index)
        {
            if (auto* live = dynamic_cast<UnitEditor*>(self.Get()); live && !live->m_Syncing && index >= 0)
            {
                live->m_Current = static_cast<std::size_t>(index);
                live->Refresh();
            }
        });
    SetValue(value);
}

void UnitEditor::SetUnits(std::vector<UnitDefinition> units)
{
    VerifyStructuralMutation();
    ValidateUnits(units);
    const auto suffix = GetUnit();
    std::vector<std::string> symbols;
    std::size_t selected = 0;
    for (std::size_t index = 0; index < units.size(); ++index)
    {
        symbols.push_back(units[index].m_Symbol);
        if (units[index].m_Symbol == suffix)
        {
            selected = index;
        }
    }
    m_Units = std::move(units);
    m_Current = selected;
    Detail::PropertySyncScope synchronizing(*this, m_Syncing);
    const WidgetRef self(this);
    m_Unit->SetItems(std::move(symbols));
    if (!self.Get())
    {
        return;
    }
    if (!m_Units.empty())
    {
        m_Unit->SetCurrentIndex(static_cast<int>(m_Current));
    }
    if (!self.Get())
    {
        return;
    }
    synchronizing.Reset();
    m_Unit->SetVisible(!m_Units.empty());
    if (self.Get())
    {
        Refresh();
    }
}

const std::vector<UnitDefinition>& UnitEditor::GetUnits() const
{
    VerifyAccess();
    return m_Units;
}

void UnitEditor::SetUnit(std::string_view symbol)
{
    VerifyStructuralMutation();
    for (std::size_t index = 0; index < m_Units.size(); ++index)
    {
        if (m_Units[index].m_Symbol == symbol)
        {
            m_Unit->SetCurrentIndex(static_cast<int>(index));
            return;
        }
    }
    if (m_Units.empty() && symbol.empty())
    {
        return;
    }
    throw std::invalid_argument("Unknown or incompatible display unit.");
}

std::string UnitEditor::GetUnit() const
{
    VerifyAccess();
    return m_Units.empty() ? std::string{} : m_Units[m_Current].m_Symbol;
}

void UnitEditor::SetRange(double minimum, double maximum)
{
    VerifyStructuralMutation();
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum < -1e12 || maximum > 1e12 || minimum > m_Value ||
        maximum < m_Value || minimum > maximum)
    {
        throw std::invalid_argument("Unit range must be finite, bounded and contain the canonical value.");
    }
    m_Minimum = minimum;
    m_Maximum = maximum;
    Refresh();
}

void UnitEditor::SetPrecision(double step, int decimals)
{
    VerifyStructuralMutation();
    if (!std::isfinite(step) || step <= 0 || decimals < 0 || decimals > 12)
    {
        throw std::invalid_argument("Invalid unit editor precision.");
    }
    const WidgetRef self(this);
    m_Editor->SetStep(step);
    if (self.Get())
    {
        m_Editor->SetDecimals(decimals);
    }
}

void UnitEditor::SetValue(double value)
{
    VerifyStructuralMutation();
    if (!std::isfinite(value) || value < m_Minimum || value > m_Maximum)
    {
        throw std::invalid_argument("Canonical unit value is outside the finite range.");
    }
    if (m_Value == value)
    {
        return;
    }
    m_Value = value;
    const WidgetRef self(this);
    Refresh();
    if (self.Get() && m_Value == value)
    {
        Detail::EmitPropertyNotification(*this, m_NotificationDepth, m_ValueChanged, value);
    }
}

double UnitEditor::GetValue() const
{
    VerifyAccess();
    return m_Value;
}

NumericInput& UnitEditor::Editor() const
{
    VerifyAccess();
    return *m_Editor;
}

Signal<double>& UnitEditor::ValueChanged()
{
    VerifyAccess();
    return m_ValueChanged;
}

void UnitEditor::Refresh()
{
    const auto unit = m_Units.empty() ? UnitDefinition{} : m_Units[m_Current];
    const double minimum = (m_Minimum - unit.m_Offset) / unit.m_Scale;
    const double maximum = (m_Maximum - unit.m_Offset) / unit.m_Scale;
    const double displayed = (m_Value - unit.m_Offset) / unit.m_Scale;
    const WidgetRef self(this);
    Detail::PropertySyncScope synchronizing(*this, m_Syncing);
    m_Editor->SetRange(minimum, maximum);
    if (!self.Get())
    {
        return;
    }
    m_Editor->SetValue(displayed);
    if (self.Get())
    {
        synchronizing.Reset();
    }
}

double UnitEditor::GetMinimum() const
{
    VerifyAccess();
    return m_Minimum;
}

double UnitEditor::GetMaximum() const
{
    VerifyAccess();
    return m_Maximum;
}

} // namespace EStudioUI
