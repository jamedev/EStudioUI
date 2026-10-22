#include <EStudioUI/NumericInput.h>
#include <EStudioUI/InputValidator.h>
#include <EStudioUI/UiContext.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace EStudioUI
{
NumericInput::NumericInput(double value) : Widget("numericinput")
{
    if (!std::isfinite(value))
        throw std::invalid_argument("Numeric input value must be finite.");
    SetLayoutDirection(Axis::Horizontal);
    m_Decrement = &Add<ToolButton>("-");
    m_Decrement->AddClass("numeric-decrement");
    m_Editor = &Add<TextBox>();
    m_Editor->AddClass("numeric-editor");
    m_Editor->SetWidth(SizeRule::Fill(1, 24));
    m_Editor->SetMaxBytes(512);
    m_Increment = &Add<ToolButton>("+");
    m_Increment->AddClass("numeric-increment");
    const WidgetRef owner(this);
    m_Decrement->Clicked().Connect(
        [owner]
        {
            if (auto* live = dynamic_cast<NumericInput*>(owner.Get()))
                live->StepBy(-1);
        });
    m_Increment->Clicked().Connect(
        [owner]
        {
            if (auto* live = dynamic_cast<NumericInput*>(owner.Get()))
                live->StepBy(1);
        });
    m_Editor->Submitted().Connect(
        [owner](const std::string&)
        {
            if (auto* live = dynamic_cast<NumericInput*>(owner.Get()))
                live->Commit();
        });
    m_Editor->FocusLost().Connect(
        [owner]
        {
            if (auto* live = dynamic_cast<NumericInput*>(owner.Get()))
                live->Commit();
        });
    m_Editor->ValidationChanged().Connect(
        [owner](ValidationState state)
        {
            if (auto* live = dynamic_cast<NumericInput*>(owner.Get()))
                live->UpdateValidationState(state);
        });
    UpdateValidationState(m_Editor->GetValidationState());
    RefreshValidator();
    m_Value = std::clamp(value, m_Minimum, m_Maximum);
    RefreshText();
}

void NumericInput::SetRange(double minimum, double maximum)
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum)
        throw std::invalid_argument("Numeric input bounds must be finite and ordered.");
    if (m_Minimum == minimum && m_Maximum == maximum)
        return;
    m_Minimum = minimum;
    m_Maximum = maximum;
    const auto revision = ++m_ValueRevision;
    const WidgetRef lifetime(this);
    RefreshValidator();
    if (lifetime.Get() && revision == m_ValueRevision)
        SetValue(m_Value);
}

double NumericInput::GetMinimum() const
{
    VerifyAccess();
    return m_Minimum;
}

double NumericInput::GetMaximum() const
{
    VerifyAccess();
    return m_Maximum;
}

void NumericInput::SetStep(double step)
{
    VerifyAccess();
    if (!std::isfinite(step) || step <= 0)
        throw std::invalid_argument("Numeric input step must be finite and positive.");
    if (m_Step == step)
        return;
    m_Step = step;
}

double NumericInput::GetStep() const
{
    VerifyAccess();
    return m_Step;
}

void NumericInput::SetDecimals(int decimals)
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    if (decimals < 0 || decimals > 12)
        throw std::invalid_argument("Numeric input decimals must lie between zero and twelve.");
    if (m_Decimals == decimals)
        return;
    m_Decimals = decimals;
    ++m_ValueRevision;
    RefreshText();
}

int NumericInput::GetDecimals() const
{
    VerifyAccess();
    return m_Decimals;
}

void NumericInput::SetValue(double value)
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    if (!std::isfinite(value))
        throw std::invalid_argument("Numeric input value must be finite.");
    value = std::clamp(value, m_Minimum, m_Maximum);
    const bool changed = value != m_Value;
    if (!changed && m_Editor->GetText() == FormatValue())
        return;
    m_Value = value;
    const auto revision = ++m_ValueRevision;
    const WidgetRef lifetime(this);
    RefreshText();
    if (lifetime.Get() && revision == m_ValueRevision && changed)
        m_ValueChanged.Emit(value);
}

double NumericInput::GetValue() const
{
    VerifyAccess();
    return m_Value;
}

TextBox& NumericInput::GetEditor()
{
    VerifyAccess();
    return *m_Editor;
}

const TextBox& NumericInput::GetEditor() const
{
    VerifyAccess();
    return *m_Editor;
}

std::string NumericInput::FormatValue() const
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(m_Decimals) << m_Value;
    double displayed = 0;
    if (!DoubleValidator::TryParse(text.str(), displayed) || displayed < m_Minimum || displayed > m_Maximum)
    {
        text.str({});
        text << std::defaultfloat << std::setprecision(std::numeric_limits<double>::max_digits10) << m_Value;
    }
    return text.str();
}

void NumericInput::RefreshText()
{
    const auto text = FormatValue();
    if (m_Editor->GetText() != text)
        m_Editor->SetText(text);
}

void NumericInput::RefreshValidator()
{
    m_Editor->SetValidator(std::make_shared<DoubleValidator>(m_Minimum, m_Maximum));
}

void NumericInput::UpdateValidationState(ValidationState state)
{
    SetState(Valid, state == ValidationState::Acceptable);
    SetState(Invalid, state == ValidationState::Invalid);
    SetState(Intermediate, state == ValidationState::Intermediate);
}

bool NumericInput::Commit()
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    double value = m_Value;
    const auto text = m_Editor->GetText();
    const bool unchangedPresentation = text == FormatValue();
    if (!unchangedPresentation &&
        (m_Editor->GetValidationState() != ValidationState::Acceptable || !DoubleValidator::TryParse(text, value)))
    {
        RefreshText();
        return false;
    }
    const WidgetRef lifetime(this);
    SetValue(value);
    if (lifetime.Get())
        m_EditingFinished.Emit(m_Value);
    return true;
}

void NumericInput::StepBy(int steps)
{
    VerifyAccess();
    const double distance = m_Step * std::abs(static_cast<double>(steps));
    if (steps > 0)
        SetValue(!std::isfinite(distance) || m_Value >= m_Maximum - distance ? m_Maximum : m_Value + distance);
    else if (steps < 0)
        SetValue(!std::isfinite(distance) || m_Value <= m_Minimum + distance ? m_Minimum : m_Value - distance);
}

Signal<double>& NumericInput::ValueChanged()
{
    VerifyAccess();
    return m_ValueChanged;
}

Signal<double>& NumericInput::EditingFinished()
{
    VerifyAccess();
    return m_EditingFinished;
}

bool NumericInput::OnKeyPreview(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || !m_Editor->IsFocused() || modifiers.m_Control || modifiers.m_Alt)
        return false;
    if (key == Key::Enter)
    {
        Commit();
        return true;
    }
    if (key == Key::Up || key == Key::Down || key == Key::PageUp || key == Key::PageDown)
    {
        const int magnitude = key == Key::PageUp || key == Key::PageDown ? 10 : 1;
        StepBy((key == Key::Up || key == Key::PageUp) ? magnitude : -magnitude);
        return true;
    }
    if (key == Key::Escape)
    {
        RefreshText();
        return true;
    }
    return false;
}

bool NumericInput::OnScroll(Point delta, Modifiers)
{
    if (!m_Editor->IsFocused() || !std::isfinite(delta.m_Y) || delta.m_Y == 0)
        return false;
    StepBy(delta.m_Y > 0 ? 1 : -1);
    return true;
}
} // namespace EStudioUI
