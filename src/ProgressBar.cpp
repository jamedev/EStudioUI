#include <EStudioUI/ProgressBar.h>
#include <EStudioUI/Renderer.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
ProgressBar::ProgressBar(std::string type) : Widget(std::move(type))
{
    RegisterStylePart(
        "track", 0,
        [](const Style& owner, unsigned)
        {
            Style s;
            s.m_Background = {0.08f, 0.12f, 0.2f, 1};
            s.m_Radius = owner.m_Radius;
            return s;
        },
        true);
    RegisterStylePart(
        "chunk", Indeterminate,
        [](const Style& owner, unsigned)
        {
            Style s;
            s.m_Background = {0.15f, 0.5f, 0.95f, 1};
            s.m_Radius = owner.m_Radius;
            return s;
        },
        true);
    RegisterStylePart("label", 0, [](const Style&, unsigned) { return Style{}; });
}

void ProgressBar::SetRange(double minimum, double maximum)
{
    VerifyStructuralMutation();
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || maximum <= minimum || !std::isfinite(maximum - minimum))
        throw std::invalid_argument("Progress requires finite increasing limits and span.");
    if (minimum == m_Minimum && maximum == m_Maximum)
        return;
    m_Minimum = minimum;
    m_Maximum = maximum;
    RequestRepaint();
    NotifySemanticsChanged();
    SetValue(m_Value);
}

void ProgressBar::SetValue(double value)
{
    VerifyStructuralMutation();
    const auto lease = RetainOwner();
    if (!std::isfinite(value))
        throw std::invalid_argument("Progress must be finite.");
    value = std::clamp(value, m_Minimum, m_Maximum);
    if (value == m_Value)
        return;
    m_Value = value;
    RequestRepaint();
    NotifySemanticsChanged();
    if (m_Notifying)
        return;
    const WidgetRef self(this);
    m_Notifying = true;
    try
    {
        unsigned turns = 0;
        while (m_Notified != m_Value)
        {
            if (++turns > 64)
                throw std::logic_error("Progress observers exceeded 64 reentrant changes.");
            m_Notified = m_Value;
            m_ValueChanged.Emit(m_Notified);
            if (!self.Get())
                return;
        }
        m_Notifying = false;
    }
    catch (...)
    {
        if (self.Get())
            m_Notifying = false;
        throw;
    }
}

double ProgressBar::GetValue() const
{
    VerifyAccess();
    return m_Value;
}

double ProgressBar::GetMinimum() const
{
    VerifyAccess();
    return m_Minimum;
}

double ProgressBar::GetMaximum() const
{
    VerifyAccess();
    return m_Maximum;
}

void ProgressBar::SetIndeterminate(bool enabled)
{
    VerifyStructuralMutation();
    if (m_Indeterminate == enabled)
        return;
    m_Indeterminate = enabled;
    SetState(Indeterminate, enabled);
    SetPartState("chunk", enabled ? Indeterminate : 0u);
    Invalidate(Invalidation::Content);
    NotifySemanticsChanged();
}

bool ProgressBar::IsIndeterminate() const
{
    VerifyAccess();
    return m_Indeterminate;
}

void ProgressBar::SetTextVisible(bool visible)
{
    VerifyStructuralMutation();
    if (m_TextVisible != visible)
    {
        m_TextVisible = visible;
        Invalidate(Invalidation::Content);
    }
}

Signal<double>& ProgressBar::ValueChanged()
{
    VerifyAccess();
    return m_ValueChanged;
}

bool ProgressBar::NeedsContinuousRendering() const
{
    return m_Indeterminate && IsEnabled();
}

void ProgressBar::OnFontsChanged() noexcept
{
    m_Caption = {};
}

Point ProgressBar::Measure(FontFace& font) const
{
    if (!m_TextVisible || m_Indeterminate)
        return {160, 22};
    const auto text = font.Shape("100%", GetPartStyle("label"));
    return {std::max(160.0f, text.m_Width), std::max(22.0f, text.m_Ascender + text.m_Descender)};
}

SemanticInfo ProgressBar::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Role = SemanticRole::ProgressBar;
    info.m_ReadOnly = true;
    info.m_Minimum = m_Minimum;
    info.m_Maximum = m_Maximum;
    info.m_Indeterminate = m_Indeterminate;
    if (!m_Indeterminate)
        info.m_Number = m_Value;
    return info;
}

void ProgressBar::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    renderer.DrawBox(GetBounds(), GetStyle(), GetClip(), opacity);
    const auto content = GetContentBounds(), clip = GetClip().Intersect(content);
    const auto& track = GetPartStyle("track");
    const auto& fill = GetPartStyle("chunk");
    renderer.PushState(track.m_Transform.Resolve(content));
    renderer.DrawBox(content, track, GetClip(), opacity * track.m_Opacity);
    renderer.PopState();
    auto chunk = content;
    const double ratio = (m_Value - m_Minimum) / (m_Maximum - m_Minimum);
    chunk.m_Width *= static_cast<float>(m_Indeterminate ? 0.28 : ratio);
    if (m_Indeterminate)
    {
        const float phase = static_cast<float>(std::fmod(std::max(0.0, IsEnabled() ? time : 0.0) * 0.7, 2.0));
        chunk.m_X += (content.m_Width - chunk.m_Width) * (phase <= 1 ? phase : 2 - phase);
    }
    renderer.PushState();
    renderer.PushClip(content, track.m_Radius);
    renderer.PushState(fill.m_Transform.Resolve(chunk));
    renderer.DrawBox(chunk, fill, clip, opacity * fill.m_Opacity);
    renderer.PopState();
    renderer.PopState();
    if (m_TextVisible && !m_Indeterminate)
    {
        const auto& style = GetPartStyle("label");
        const int percent = static_cast<int>(std::lround(ratio * 100));
        if (percent != m_Percent || !font.Matches(m_Caption, style))
        {
            m_Percent = percent;
            m_Caption = font.Shape(std::to_string(percent) + "%", style);
        }
        auto color = style.m_Color;
        color.m_A *= opacity * style.m_Opacity;
        renderer.PushState(style.m_Transform.Resolve(content));
        renderer.DrawText(m_Caption,
                          {content.m_X + (content.m_Width - m_Caption.m_Width) * 0.5f,
                           content.m_Y + (content.m_Height - m_Caption.m_Ascender - m_Caption.m_Descender) * 0.5f +
                               m_Caption.m_Ascender},
                          color, clip);
        renderer.PopState();
    }
}
} // namespace EStudioUI
