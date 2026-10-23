#pragma once

#include "ComboBox.h"
#include "NumericInput.h"
#include "PropertyValue.h"

namespace EStudioUI
{
//! A scalar editor whose canonical value is independent of its selected display unit.
class UnitEditor : public Widget
{
  public:
    //! Creates a dimensionless finite scalar editor.
    explicit UnitEditor(double value = 0);

    //! Replaces compatible units atomically; an empty list restores dimensionless display.
    void SetUnits(std::vector<UnitDefinition> units);

    //! Returns compatible display units.
    const std::vector<UnitDefinition>& GetUnits() const;

    //! Selects a display suffix without emitting a canonical value change.
    void SetUnit(std::string_view symbol);

    //! Returns the selected suffix, or empty for dimensionless display.
    std::string GetUnit() const;

    //! Returns the inclusive canonical minimum.
    double GetMinimum() const;

    //! Returns the inclusive canonical maximum.
    double GetMaximum() const;

    //! Sets inclusive canonical bounds without silently changing the current value.
    void SetRange(double minimum, double maximum);

    //! Sets positive display stepping and zero-to-twelve decimal places.
    void SetPrecision(double step, int decimals);

    //! Sets a finite in-range canonical value, rejecting rather than clamping.
    void SetValue(double value);

    //! Returns the canonical value.
    double GetValue() const;

    //! Returns the embedded display-value editor.
    NumericInput& Editor() const;

    //! Emits canonical values after committed changes; suffix changes do not emit.
    Signal<double>& ValueChanged();

  private:
    //! Synchronizes display ranges and values with deletion-safe callback checks.
    void Refresh();

    //! Observes the owned numeric editor.
    NumericInput* m_Editor = nullptr;

    //! Observes the owned unit chooser.
    ComboBox* m_Unit = nullptr;

    //! Holds a bounded compatible unit catalog.
    std::vector<UnitDefinition> m_Units;

    //! Selected display-unit index.
    std::size_t m_Current = 0;

    //! Canonical finite scalar.
    double m_Value = 0;

    //! Inclusive canonical bounds.
    double m_Minimum = -1e12, m_Maximum = 1e12;

    //! Suppresses display synchronization feedback.
    bool m_Syncing = false;

    //! Reports committed canonical changes.
    Signal<double> m_ValueChanged;
    //! Bounds synchronous reentrant observer delivery.
    unsigned m_NotificationDepth = 0;
};
} // namespace EStudioUI
