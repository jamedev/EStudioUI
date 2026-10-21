#include <EStudioUI/InputValidator.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/UiContext.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const char* message)
{
    ++s_Checks;
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Callback> void Throws(Callback callback, const char* message)
{
    bool threw = false;
    try
    {
        callback();
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    Check(threw, message);
}

bool Near(double a, double b)
{
    return std::abs(a - b) < 1e-9;
}

void Validators()
{
    DoubleValidator number(-100, 100);
    for (const auto* text : {"0", "-25.5", "+.5", "1e2", "-1E-2", "10."})
        Check(number.Validate(text) == ValidationState::Acceptable, "complete finite numeric input accepted");
    for (const auto* text : {"", "-", "+", ".", "-.", "1e", "1e-", "101", "-101"})
        Check(number.Validate(text) == ValidationState::Intermediate, "numeric editing prefix/range intermediate");
    for (const auto* text :
         {"NaN", "nan", "inf", "Infinity", "1,2", " 1", "1 ", "1x", "--1", "+-1", "1e9999", "1e--2", "e3"})
        Check(number.Validate(text) == ValidationState::Invalid, "malformed or nonfinite numeric input rejected");
    double value = 44;
    Check(!DoubleValidator::TryParse("NaN", value) && value == 44, "failed parse preserves output");
    Check(DoubleValidator::TryParse("+1.25e2", value) && value == 125, "strict decimal parser supports plus/exponent");
    IntegerValidator integer(-12, 12);
    Check(integer.Validate("+12") == ValidationState::Acceptable, "integer plus sign");
    Check(integer.Validate("-12") == ValidationState::Acceptable, "integer negative sign");
    Check(integer.Validate("13") == ValidationState::Intermediate, "integer out-of-range remains editable");
    Check(integer.Validate("99999999999999999999999999999") == ValidationState::Intermediate,
          "integer overflow never wraps");
    Check(integer.Validate("+-1") == ValidationState::Invalid && integer.Validate("1.0") == ValidationState::Invalid,
          "integer rejects doubled sign and decimals");
    Throws([] { DoubleValidator invalid(2, 1); }, "inverted numeric range rejected");
    Throws([] { DoubleValidator invalid(0, std::numeric_limits<double>::infinity()); }, "nonfinite range rejected");
}

void ValidatedEditor(FontFace& font)
{
    UiContext ui(font);
    std::string clipboard;
    ui.SetClipboard([&] { return clipboard; }, [&](std::string_view text) { clipboard = text; });
    auto& editor = ui.Root().Add<TextBox>("12");
    editor.SetValidator(std::make_shared<DoubleValidator>(-100, 100));
    ui.Prepare(400, 100);
    ui.SetFocus(&editor);
    int submissions = 0;
    editor.Submitted().Connect([&](const auto&) { ++submissions; });
    editor.SelectAll();
    ui.TextInput("abc");
    Check(editor.GetText() == "12" && editor.GetSelectedText() == "12",
          "invalid insertion preserves text and selection");
    ui.TextInput("-");
    Check(editor.GetText() == "-" && editor.GetValidationState() == ValidationState::Intermediate,
          "intermediate numeric edit accepted");
    ui.KeyEvent(Key::Enter, true);
    Check(submissions == 0, "intermediate editor cannot submit");
    ui.TextInput("5");
    ui.KeyEvent(Key::Enter, true);
    Check(editor.GetText() == "-5" && submissions == 1, "acceptable numeric edit submits");
    ui.SetClipboardText("NaN");
    editor.SelectAll();
    ui.KeyEvent(Key::V, true, false, {false, true, false});
    Check(editor.GetText() == "-5" && editor.GetSelectedText() == "-5", "invalid paste preserves selection and text");
    ui.KeyEvent(Key::Z, true, false, {false, true, false});
    Check(editor.GetText() == "-", "invalid paste creates no undo entry");
    ui.KeyEvent(Key::Y, true, false, {false, true, false});
    Check(editor.GetText() == "-5", "validated redo restores edit");
    Throws([&] { editor.SetText("inf"); }, "programmatic invalid text rejected");
    Check(editor.GetText() == "-5", "invalid programmatic set is atomic");
    editor.SetText("40");
    editor.SetValidator(std::make_shared<IntegerValidator>(0, 10));
    Check(editor.GetValidationState() == ValidationState::Intermediate,
          "changing validator classifies existing content");
    editor.SetValidator(nullptr);
    editor.SetText("free text");
    Check(editor.GetValidationState() == ValidationState::Acceptable, "null validator restores unrestricted editor");
}

void NumericEditor(FontFace& font)
{
    UiContext ui(font);
    auto& numeric = ui.Root().Add<NumericInput>(2.5);
    numeric.SetWidth(SizeRule::Fixed(250));
    numeric.SetRange(-10, 10);
    numeric.SetStep(0.25);
    numeric.SetDecimals(2);
    auto& other = ui.Root().Add<Button>("Next");
    ui.Prepare(600, 200);
    int changes = 0;
    int finishes = 0;
    numeric.ValueChanged().Connect([&](double) { ++changes; });
    numeric.EditingFinished().Connect([&](double) { ++finishes; });
    auto& editor = numeric.GetEditor();
    ui.SetFocus(&editor);
    editor.SelectAll();
    ui.TextInput("7.75");
    Check(Near(numeric.GetValue(), 2.5), "typed numeric input stays uncommitted until commit");
    ui.KeyEvent(Key::Enter, true);
    Check(Near(numeric.GetValue(), 7.75) && changes == 1 && finishes == 1, "Enter commits numeric text once");
    editor.SelectAll();
    ui.TextInput("-");
    ui.KeyEvent(Key::Enter, true);
    Check(editor.GetText() == "7.75" && numeric.GetValue() == 7.75 && finishes == 1,
          "invalid Enter restores previous value");
    editor.SelectAll();
    ui.TextInput("50");
    ui.SetFocus(&other);
    Check(numeric.GetValue() == 7.75 && editor.GetText() == "7.75", "out-of-range focus loss rolls back");
    ui.SetFocus(&editor);
    ui.KeyEvent(Key::Up, true);
    Check(numeric.GetValue() == 8, "up key increments numeric input");
    ui.Scroll({100, 15}, {0, -1});
    Check(numeric.GetValue() == 7.75, "focused numeric wheel decrements value");
    editor.SelectAll();
    ui.TextInput("9");
    ui.KeyEvent(Key::Escape, true);
    Check(editor.GetText() == "7.75", "Escape restores pending numeric edit");
    numeric.SetValue(100);
    Check(numeric.GetValue() == 10, "programmatic numeric value clamps to range");
    numeric.StepBy(std::numeric_limits<int>::max());
    Check(numeric.GetValue() == 10, "large numeric step saturates");
    numeric.SetRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    numeric.SetStep(std::numeric_limits<double>::max());
    numeric.StepBy(std::numeric_limits<int>::min());
    Check(numeric.GetValue() == -std::numeric_limits<double>::max(), "numeric huge step avoids overflow");
    numeric.SetRange(1.23456789, 1.23456789);
    Check(numeric.GetEditor().GetValidationState() == ValidationState::Acceptable,
          "format preserves a narrow range endpoint");
    numeric.Commit();
    Check(numeric.GetValue() == 1.23456789, "unchanged displayed text preserves exact stored precision");
    Throws([&] { numeric.SetStep(0); }, "zero numeric step rejected");
    Throws([&] { numeric.SetDecimals(50); }, "excessive decimal precision rejected");
    Throws([&] { numeric.SetValue(std::numeric_limits<double>::quiet_NaN()); }, "numeric NaN rejected");
}

void Sliders(FontFace& font)
{
    UiContext ui(font);
    auto& slider = ui.Root().Add<Slider>();
    slider.SetWidth(SizeRule::Fixed(200));
    slider.SetHeight(SizeRule::Fixed(24));
    slider.SetRange(0, 10);
    slider.SetStep(0.5);
    ui.Prepare(300, 100);
    int releases = 0;
    slider.SliderReleased().Connect([&] { ++releases; });
    ui.PointerDown({100, 12});
    ui.PointerMove({1000, 12});
    ui.PointerUp({1000, 12});
    Check(slider.GetValue() == 10 && releases == 1, "slider drag capture clamps outside and releases once");
    ui.KeyEvent(Key::Home, true);
    ui.KeyEvent(Key::Right, true);
    Check(slider.GetValue() == 0.5, "slider keyboard home and step");
    ui.KeyEvent(Key::PageUp, true);
    Check(slider.GetValue() == 5.5, "slider page command steps ten increments");
    slider.SetOrientation(Axis::Vertical);
    slider.SetWidth(SizeRule::Fixed(24));
    slider.SetHeight(SizeRule::Fixed(100));
    ui.Prepare(300, 200);
    ui.PointerDown({12, 8});
    ui.PointerUp({12, 8});
    Check(slider.GetValue() == 10, "vertical slider maximum is at top");
    slider.SetRange(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    slider.SetStep(std::numeric_limits<double>::max());
    ui.PointerDown({12, 37});
    ui.PointerUp({12, 37});
    Check(slider.GetValue() == 0, "pointer quantization survives an overflowing range span");
    slider.SetValue(0);
    ui.KeyEvent(Key::PageUp, true);
    Check(slider.GetValue() == std::numeric_limits<double>::max(), "slider overflow-resistant increment");
    ui.KeyEvent(Key::PageDown, true);
    Check(slider.GetValue() == -std::numeric_limits<double>::max(), "slider overflow-resistant decrement");
    slider.SetRange(2, 2);
    ui.PointerDown({12, 50});
    ui.PointerUp({12, 50});
    Check(slider.GetValue() == 2, "zero-span slider remains finite and fixed");
}

void Lifetimes(FontFace& font)
{
    UiContext ui(font);
    auto& slider = ui.Root().Add<Slider>();
    slider.SetWidth(SizeRule::Fixed(200));
    slider.SetHeight(SizeRule::Fixed(24));
    const WidgetRef sliderRef(&slider);
    slider.ValueChanged().Connect([&](double) { ui.Root().RemoveChild(slider); });
    ui.Prepare(300, 100);
    ui.PointerDown({100, 12});
    ui.PointerUp({100, 12});
    Check(!sliderRef.Get(), "slider callback may delete sender during captured input");
    auto& numeric = ui.Root().Add<NumericInput>();
    const WidgetRef numericRef(&numeric);
    numeric.ValueChanged().Connect([&](double) { ui.Root().RemoveChild(numeric); });
    ui.Prepare(300, 100);
    numeric.GetEditor().SetText("3");
    numeric.Commit();
    Check(!numericRef.Get(), "numeric commit callback may delete composite and editor");
    auto& nested = ui.Root().Add<NumericInput>();
    int changes = 0;
    nested.ValueChanged().Connect([&](double) { ++changes; });
    nested.GetEditor().TextChanged().Connect(
        [&](const std::string& text)
        {
            if (text == "2.000000")
                nested.SetValue(3);
        });
    nested.SetValue(2);
    Check(nested.GetValue() == 3 && changes == 1, "reentrant text callback suppresses stale numeric emission");
    for (const std::size_t childIndex : {0u, 2u})
    {
        auto& composite = ui.Root().Add<NumericInput>();
        const WidgetRef compositeRef(&composite);
        auto& command = dynamic_cast<ToolButton&>(*composite.GetChildren()[childIndex]);
        command.SetCheckable(true);
        command.Toggled().Connect([&](bool) { ui.Root().RemoveChild(composite); });
        command.Clicked().Emit();
        Check(!compositeRef.Get(), "earlier nested button callback may delete the composite before its queued step");
    }
}
} // namespace

int main()
{
    try
    {
        FontFace font(ESTUDIOUI_FONT_PATH);
        Validators();
        ValidatedEditor(font);
        NumericEditor(font);
        Sliders(font);
        Lifetimes(font);
        std::cout << "Input controls PASS / " << s_Checks << " checks\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Input controls FAIL / " << error.what() << '\n';
        return 1;
    }
}
