#include "PropertyNotification.h"
#include <EStudioUI/VectorEditor.h>
#include <EStudioUI/Label.h>

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
VectorEditor::VectorEditor(std::size_t components) : Widget("vectoreditor"), m_Value(components, 0)
{
    if (components < 2 || components > 4)
    {
        throw std::invalid_argument("Vector editors require two through four components.");
    }
    SetLayoutDirection(Axis::Horizontal);
    SetGap(6);
    const WidgetRef self(this);
    constexpr const char* labels[]{"X", "Y", "Z", "W"};
    for (std::size_t index = 0; index < components; ++index)
    {
        auto& field = Add<Widget>("vector-component");
        field.SetWidth(SizeRule::Fill(1, 48));
        field.SetLayoutDirection(Axis::Vertical);
        field.SetGap(2);
        field.Add<Label>(labels[index], "vector-label");
        auto& editor = field.Add<NumericInput>();
        editor.SetAccessibleName(labels[index]);
        editor.SetDecimals(3);
        editor.SetWidth(SizeRule::Fill(1, 40));
        field.SetBaselineChild(&editor);
        m_Editors.push_back(&editor);
        editor.ValueChanged().Connect(
            [self, index](double value)
            {
                if (auto* live = dynamic_cast<VectorEditor*>(self.Get()); live && !live->m_Syncing)
                {
                    auto candidate = live->m_Value;
                    candidate[index] = value;
                    live->SetValue(std::move(candidate));
                }
            });
    }
    SetBaselineChild(GetChildren().front().get());
}

void VectorEditor::SetValue(VectorValue value)
{
    VerifyStructuralMutation();
    if (value.size() != m_Value.size())
    {
        throw std::invalid_argument("Vector component count cannot change.");
    }
    for (const double component : value)
    {
        if (!std::isfinite(component) || component < m_Minimum || component > m_Maximum)
        {
            throw std::invalid_argument("Every vector component must be finite and in range.");
        }
    }
    if (m_Value == value)
    {
        return;
    }
    m_Value = value;
    const auto revision = ++m_Revision;
    const WidgetRef self(this);
    Detail::PropertySyncScope synchronizing(*this, m_Syncing);
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        m_Editors[index]->SetValue(value[index]);
        if (!self.Get() || revision != m_Revision)
        {
            return;
        }
    }
    synchronizing.Reset();
    Detail::EmitPropertyNotification(*this, m_NotificationDepth, m_ValueChanged, value);
}

VectorValue VectorEditor::GetValue() const
{
    VerifyAccess();
    return m_Value;
}

void VectorEditor::SetRange(double minimum, double maximum)
{
    VerifyStructuralMutation();
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum < -1e12 || maximum > 1e12 || minimum > maximum)
    {
        throw std::invalid_argument("Invalid finite vector range.");
    }
    for (const double component : m_Value)
    {
        if (component < minimum || component > maximum)
        {
            throw std::invalid_argument("New vector range must contain every canonical component.");
        }
    }
    m_Minimum = minimum;
    m_Maximum = maximum;
    const WidgetRef self(this);
    for (auto* editor : m_Editors)
    {
        editor->SetRange(minimum, maximum);
        if (!self.Get())
        {
            return;
        }
    }
}

void VectorEditor::SetPrecision(double step, int decimals)
{
    VerifyStructuralMutation();
    if (!std::isfinite(step) || step <= 0 || decimals < 0 || decimals > 12)
    {
        throw std::invalid_argument("Invalid vector editor precision.");
    }
    const WidgetRef self(this);
    for (auto* editor : m_Editors)
    {
        editor->SetStep(step);
        editor->SetDecimals(decimals);
        if (!self.Get())
        {
            return;
        }
    }
}

NumericInput& VectorEditor::Component(std::size_t index) const
{
    VerifyAccess();
    return *m_Editors.at(index);
}

Signal<const VectorValue&>& VectorEditor::ValueChanged()
{
    VerifyAccess();
    return m_ValueChanged;
}

double VectorEditor::GetMinimum() const
{
    VerifyAccess();
    return m_Minimum;
}

double VectorEditor::GetMaximum() const
{
    VerifyAccess();
    return m_Maximum;
}

} // namespace EStudioUI
