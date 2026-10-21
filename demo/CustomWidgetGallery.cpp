#include "CustomWidgetGallery.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
constexpr float s_Start = -1.25f * std::numbers::pi_v<float>;
constexpr float s_Sweep = 1.5f * std::numbers::pi_v<float>;

Point Along(float angle, float radius)
{
    return {std::cos(angle) * radius, std::sin(angle) * radius};
}

void Check(bool condition, const std::string& message)
{
    if (!condition)
        throw std::runtime_error("Custom widgets: " + message);
}
} // namespace

DemoDial::DemoDial() : PaintWidget("demo-dial")
{
    m_Track.ArcTo({0, 0}, .83f, s_Start, s_Sweep);
    m_Wave.MoveTo({-.55f, .55f});
    m_Wave.CubicTo({-.25f, .85f}, {.2f, .25f}, {.55f, .55f});
}

void DemoDial::SetValue(double value)
{
    if (!std::isfinite(value))
        throw std::invalid_argument("Dial values must be finite.");
    value = std::clamp(value, 0.0, 100.0);
    if (m_Value == value)
        return;
    m_Value = value;
    InvalidatePainting();
    m_ValueChanged.Emit(value);
}

double DemoDial::GetValue() const
{
    return m_Value;
}

Signal<double>& DemoDial::ValueChanged()
{
    return m_ValueChanged;
}

bool DemoDial::IsFocusable() const
{
    return true;
}

Point DemoDial::Measure(FontFace&) const
{
    return {240, 240};
}

void DemoDial::PaintContent(Painter& painter) const
{
    const auto bounds = GetPaintBounds();
    const float radius = std::min(bounds.m_Width, bounds.m_Height) * .45f;
    if (radius < 1)
        return;
    const Point center{bounds.m_Width * .5f, bounds.m_Height * .5f};
    const auto& style = GetStyle();
    const Color accent = style.m_SelectionBackground;
    painter.Save();
    painter.Translate(center.m_X, center.m_Y);
    painter.Scale(radius, radius);
    painter.SetBrush(Color{.04f, .075f, .12f, 1});
    painter.SetPen(IsFocused() ? accent : style.m_BorderColor, 2 / radius);
    painter.DrawCircle({0, 0}, 1);
    painter.SetPen({.20f, .27f, .36f, 1}, 7 / radius);
    painter.DrawPath(m_Track);
    for (int tick = 0; tick <= 20; ++tick)
    {
        const float angle = s_Start + s_Sweep * static_cast<float>(tick) / 20;
        painter.SetPen(tick * 5 <= m_Value ? accent : Color{.34f, .4f, .48f, 1}, 2 / radius);
        painter.DrawLine(Along(angle, tick % 5 == 0 ? .64f : .71f), Along(angle, .78f));
    }
    painter.SetPen(accent, 2 / radius);
    painter.DrawPath(m_Wave);
    painter.Save();
    painter.Rotate(s_Start + s_Sweep * static_cast<float>(m_Value / 100));
    painter.SetPen(accent, 4 / radius);
    painter.DrawLine({-.12f, 0}, {.58f, 0});
    painter.Restore();
    painter.SetBrush(accent);
    painter.SetPen({0, 0, 0, 0}, 0);
    painter.DrawCircle({0, 0}, .065f);
    painter.Restore();

    const auto text = std::to_string(static_cast<int>(std::round(m_Value)));
    const float fontSize = std::max(16.0f, style.m_FontSize * 1.5f);
    if (text != m_ValueText || fontSize != m_FontSize)
    {
        m_ValueLayout = painter.GetFont().Shape(text, fontSize);
        m_ValueText = text;
        m_FontSize = fontSize;
    }
    painter.SetPen(style.m_Color);
    painter.DrawText(m_ValueLayout, {center.m_X - m_ValueLayout.m_Width * .5f, center.m_Y + radius * .82f});
}

void DemoDial::SetFromPointer(Point point)
{
    const auto bounds = GetContentBounds();
    const float x = point.m_X - bounds.m_X - bounds.m_Width * .5f;
    const float y = point.m_Y - bounds.m_Y - bounds.m_Height * .5f;
    if (std::hypot(x, y) < 2)
        return;
    float angle = std::atan2(y, x);
    if (angle > std::numbers::pi_v<float> * .5f)
        angle -= 2 * std::numbers::pi_v<float>;
    SetValue(std::round(std::clamp((angle - s_Start) / s_Sweep, 0.0f, 1.0f) * 100));
}

void DemoDial::OnPointerDown(const PointerEvent& event)
{
    m_Dragging = true;
    SetState(Pressed, true);
    SetFromPointer(event.m_Position);
}

void DemoDial::OnPointerMove(const PointerEvent& event)
{
    if (m_Dragging)
        SetFromPointer(event.m_Position);
}

void DemoDial::OnPointerUp(const PointerEvent& event)
{
    if (!m_Dragging)
        return;
    m_Dragging = false;
    SetState(Pressed, false);
    SetFromPointer(event.m_Position);
}

void DemoDial::OnCaptureLost()
{
    m_Dragging = false;
    SetState(Pressed, false);
}

void DemoDial::OnKey(Key key, bool down, bool, Modifiers modifiers)
{
    if (!down || modifiers.m_Control || modifiers.m_Alt)
        return;
    if (key == Key::Left || key == Key::Down)
        SetValue(m_Value - 1);
    else if (key == Key::Right || key == Key::Up)
        SetValue(m_Value + 1);
    else if (key == Key::Home)
        SetValue(0);
    else if (key == Key::End)
        SetValue(100);
}

LabeledNumericInput::LabeledNumericInput(std::string label, double value) : Widget("labeled-number")
{
    SetLayoutDirection(Axis::Vertical);
    SetGap(8);
    Add<Label>(std::move(label));
    m_Input = &Add<NumericInput>(value);
    m_Input->SetWidth(SizeRule::Fill());
}

NumericInput& LabeledNumericInput::GetInput()
{
    return *m_Input;
}

CustomWidgetGallery::CustomWidgetGallery(Window& window, Widget& page) : m_Window(window)
{
    page.SetGap(12);
    page.Add<Label>("Your controls, the same GPU renderer").AddClass("heading");
    page.Add<Label>(
        "Drag the dial, use its arrow keys, or edit the number. Every control shares a typed value signal.");
    auto& row = page.Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetGap(24);
    row.SetHeight(SizeRule::Fixed(290));
    m_Dial = &row.Add<DemoDial>();
    m_Dial->SetWidth(SizeRule::Fixed(280));
    m_Dial->SetHeight(SizeRule::Fill());
    m_Dial->SetId("custom-dial");
    auto& inputs = row.Add<Widget>();
    inputs.SetWidth(SizeRule::Fill());
    inputs.SetGap(14);
    auto& composed = inputs.Add<LabeledNumericInput>("Composed input", 42);
    m_Input = &composed.GetInput();
    m_Input->SetRange(0, 100);
    m_Input->SetDecimals(0);
    m_Input->SetStep(1);
    m_Slider = &inputs.Add<Slider>();
    m_Slider->SetWidth(SizeRule::Fill());
    m_Slider->SetRange(0, 100);
    m_Slider->SetStep(1);
    m_Slider->SetValue(42);
    inputs.Add<Label>("The dial derives from PaintWidget.");
    inputs.Add<Label>("The labeled editor composes existing widgets.");
    inputs.Add<Label>("CSS still controls their backgrounds, colors, fonts and focus states.");
    page.Add<Label>(
        "Painter records circles, rounded rectangles, lines, curves and shaped text into the existing GPU batch.");
    page.Add<Label>("Custom controls retain normal focus, capture, child ownership and CSS behavior.");
    m_Connections.push_back(m_Dial->ValueChanged().Connect([this](double value) { SetValue(value); }));
    m_Connections.push_back(m_Slider->ValueChanged().Connect([this](double value) { SetValue(value); }));
    m_Connections.push_back(m_Input->ValueChanged().Connect([this](double value) { SetValue(value); }));
}

CustomWidgetGallery::~CustomWidgetGallery()
{
    for (const auto& connection : m_Connections)
        connection.Disconnect();
}

void CustomWidgetGallery::SetValue(double value)
{
    if (m_Updating)
        return;

    struct UpdateScope
    {
        bool& m_Flag;

        explicit UpdateScope(bool& flag) : m_Flag(flag)
        {
            m_Flag = true;
        }

        ~UpdateScope()
        {
            m_Flag = false;
        }
    } update(m_Updating);

    m_Dial->SetValue(value);
    m_Slider->SetValue(value);
    m_Input->SetValue(value);
}

void CustomWidgetGallery::Verify(const std::filesystem::path& output)
{
    auto& ui = m_Window.Ui();
    m_Window.RenderFrame();
    m_Input->SetValue(63);
    Check(m_Dial->GetValue() == 63 && m_Slider->GetValue() == 63, "composed input synchronizes controls");
    ui.SetFocus(m_Dial);
    ui.KeyEvent(Key::End, true);
    Check(m_Input->GetValue() == 100 && m_Slider->GetValue() == 100, "custom keyboard event emits typed signal");
    const auto bounds = m_Dial->GetContentBounds();
    const Point top{bounds.m_X + bounds.m_Width * .5f, bounds.m_Y + bounds.m_Height * .1f};
    ui.PointerDown(top);
    ui.PointerUp(top);
    Check(m_Dial->GetValue() == 50 && m_Input->GetValue() == 50, "custom captured pointer changes value");
    m_Window.RenderFrame();
    m_Window.Graphics().SavePpm(output / "custom-widgets.ppm");
    std::ofstream report(output / "custom-widgets.txt");
    report << "Custom GPU widget gallery: PASS\nDerived dial keyboard/pointer input, composed numeric input, "
              "typed signal synchronization, cached paths and text.\n";
}
} // namespace EStudioUI
