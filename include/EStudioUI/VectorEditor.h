#pragma once

#include "NumericInput.h"
#include "PropertyValue.h"

namespace EStudioUI
{
//! A composed two-to-four component editor with atomic canonical vector updates.
class VectorEditor : public Widget
{
  public:
    //! Creates labeled X/Y/Z/W components with finite zero defaults.
    explicit VectorEditor(std::size_t components = 3);

    //! Validates all components before changing any canonical component.
    void SetValue(VectorValue value);

    //! Returns an owned complete canonical vector.
    VectorValue GetValue() const;

    //! Returns the inclusive canonical minimum.
    double GetMinimum() const;

    //! Returns the inclusive canonical maximum.
    double GetMaximum() const;

    //! Sets a shared finite range containing every current component.
    void SetRange(double minimum, double maximum);

    //! Sets component display precision and stepping.
    void SetPrecision(double step, int decimals);

    //! Returns one ordinary numeric editor for composition and accessibility.
    NumericInput& Component(std::size_t index) const;

    //! Emits an owned complete snapshot, including reentrant updates.
    Signal<const VectorValue&>& ValueChanged();

  private:
    //! Canonical complete vector.
    VectorValue m_Value;

    //! Observes owned component editors.
    std::vector<NumericInput*> m_Editors;

    //! Inclusive component bounds.
    double m_Minimum = -1e12, m_Maximum = 1e12;

    //! Suppresses component synchronization feedback.
    bool m_Syncing = false;

    //! Invalidates stale outer synchronization after callback reentry.
    std::uint64_t m_Revision = 0;

    //! Reports complete committed values.
    Signal<const VectorValue&> m_ValueChanged;
    //! Bounds synchronous reentrant observer delivery.
    unsigned m_NotificationDepth = 0;
};
} // namespace EStudioUI
