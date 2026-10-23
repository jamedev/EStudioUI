#include <EStudioUI/Bindings.h>
#include <EStudioUI/CollapsiblePanel.h>
#include <EStudioUI/ColorEditor.h>
#include <EStudioUI/PropertyGrid.h>
#include <EStudioUI/SemanticTree.h>
#include <EStudioUI/UnitEditor.h>
#include <EStudioUI/VectorEditor.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace
{
using namespace EStudioUI;
int s_Checks = 0;

void Check(bool condition, const char* description)
{
    ++s_Checks;
    if (!condition)
    {
        throw std::runtime_error(description);
    }
}

template <typename F> void Throws(F action, const char* description)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, description);
}

void Units(UiContext& ui)
{
    auto& editor = ui.Root().Add<UnitEditor>(.0254);
    editor.SetUnits({{"m", "length", 1, 0}, {"mm", "length", .001, 0}, {"in", "length", .0254, 0}});
    int changes = 0;
    editor.ValueChanged().Connect([&](double) { ++changes; });
    editor.SetUnit("mm");
    Check(std::abs(editor.Editor().GetValue() - 25.4) < 1e-10, "Canonical length converts to millimetres");
    editor.SetUnit("in");
    Check(std::abs(editor.Editor().GetValue() - 1) < 1e-10 && changes == 0,
          "Unit switching preserves canonical values without spurious changes");
    editor.Editor().SetValue(2);
    Check(std::abs(editor.GetValue() - .0508) < 1e-10 && changes == 1, "Display edits emit canonical base units once");
    const auto units = editor.GetUnits();
    Throws([&] { editor.SetUnits({{"m", "length", 1, 0}, {"s", "time", 1, 0}}); },
           "Incompatible dimensions reject atomically");
    Check(editor.GetUnits() == units, "Rejected unit replacement preserves the complete catalog");
    Throws([&] { editor.SetUnits({{"x", "length", 0, 0}}); }, "Zero conversion scales reject before use");
    Throws([&] { editor.SetUnit("kg"); }, "Unregistered display suffixes reject");
    auto& temperature = ui.Root().Add<UnitEditor>(273.15);
    temperature.SetUnits({{"K", "temperature", 1, 0}, {"C", "temperature", 1, 273.15}});
    temperature.SetUnit("C");
    Check(std::abs(temperature.Editor().GetValue()) < 1e-10, "Affine temperature conversion supports offsets");
    State<double> canonical(ui.States(), .0508,
                            [](double value)
                            {
                                if (value > .1)
                                {
                                    throw std::invalid_argument("Too long");
                                }
                            });
    auto binding = BindValue(editor, canonical);
    editor.SetValue(.2);
    Check(editor.GetValue() == .0508 && canonical.Get() == .0508 && binding.GetError(),
          "Rejected bound unit values roll back coherently");
}

void VectorsAndColors(UiContext& ui)
{
    auto& vector = ui.Root().Add<VectorEditor>(3);
    vector.SetValue({1, 2, 3});
    Throws([&] { vector.SetValue({4, std::numeric_limits<double>::quiet_NaN(), 6}); },
           "Nonfinite vector components reject");
    Check(vector.GetValue() == VectorValue({1, 2, 3}) && vector.Component(0).GetValue() == 1,
          "Vector rejection changes no component");
    Throws([&] { vector.SetValue({1, 2}); }, "Vector shape cannot change during an edit");
    bool coherent = false;
    vector.Component(0).ValueChanged().Connect([&](double) { coherent = vector.GetValue() == VectorValue({4, 5, 6}); });
    vector.SetValue({4, 5, 6});
    Check(coherent, "All canonical components commit before child observers run");
    auto& color = ui.Root().Add<ColorEditor>();
    color.Editor().SetText("#3af8");
    Check(color.Commit() && ColorToHex(color.GetColor()) == "#33AAFF88", "Short CSS hex input expands RGBA correctly");
    const auto previous = color.GetColor();
    color.Editor().SetText("#12xyz");
    Check(!color.Commit() && color.GetColor() == previous,
          "Invalid intermediate color input preserves canonical color");
    Throws([&] { color.SetColor({1.1f, 0, 0, 1}); }, "Out-of-range color components reject");
    const Color precise{.1234567f, .3456789f, .5678912f, .7891234f};
    color.SetColor(precise);
    int passiveChanges = 0;
    const auto precisionObserver = color.ColorChanged().Connect([&](Color) { ++passiveChanges; });
    color.Editor().FocusLost().Emit();
    Check(color.GetColor() == precise && passiveChanges == 0,
          "Unedited hexadecimal presentation preserves full canonical float precision on focus loss");
    precisionObserver.Disconnect();
    auto& picker = ui.Root().Add<ColorPicker>();
    picker.SetColor({1, 0, 0, .5f});
    picker.SetModel(ColorModel::Hsv);
    picker.Channel(0).SetValue(120);
    Check(picker.GetColor().m_G > .999f && picker.GetColor().m_R < .001f && picker.GetColor().m_A == .5f,
          "HSV channel edits produce canonical green and preserve alpha");
    auto& bounded = ui.Root().Add<VectorEditor>(2);
    bounded.ValueChanged().Connect([&](const VectorValue& value) { bounded.SetValue({value[0] == 0 ? 1.0 : 0.0, 0}); });
    Throws([&] { bounded.SetValue({1, 0}); }, "Reentrant editor feedback is bounded before unbounded stack growth");
    auto& recovered = ui.Root().Add<ColorEditor>();
    auto fail = recovered.Editor().TextChanged().Connect([](const std::string&)
                                                         { throw std::runtime_error("Injected observer failure"); });
    Throws([&] { recovered.SetColor({.1f, .2f, .3f, 1}); },
           "Observer failures propagate without leaving suppression armed");
    fail.Disconnect();
    recovered.Editor().SetText("#00FF00");
    Check(recovered.Commit() && recovered.GetColor().m_G == 1, "Color editing recovers after an observer throws");
    State<Color> canonical(ui.States(), previous);
    auto binding = BindColor(color, canonical);
    color.SetColor({.2f, .3f, .4f, 1});
    Check(canonical.Get() == color.GetColor() && binding.IsConnected(),
          "Color binding shares normalized canonical values");
}

void Inspector(UiContext& ui)
{
    auto& section = ui.Root().Add<CollapsiblePanel>("Geometry");
    auto& content = section.Content().Add<TextBox>("retained");
    section.SetExpanded(false);
    Check(!content.IsVisible() && !section.IsExpanded(), "Collapsed sections remove content from visible layout");
    section.SetVisible(false);
    section.SetExpanded(true);
    Check(section.IsExpanded() && !content.IsVisible(), "Configured expansion is independent of ancestor visibility");
    section.SetVisible(true);
    Check(content.GetText() == "retained" && content.IsVisible(), "Expanding retains widget identity and content");
    std::vector<PropertyDefinition> schema;
    for (int index = 0; index < 10000; ++index)
    {
        PropertyDefinition property("property" + std::to_string(index), "Value " + std::to_string(index), "Data",
                                    PropertyKind::Number, 1.0);
        property.m_Minimum = 0;
        property.m_Maximum = 10;
        schema.push_back(std::move(property));
    }
    auto model = std::make_shared<PropertyModel>(ui.States(), std::move(schema));
    auto& grid = ui.Root().Add<PropertyGrid>();
    grid.SetHeight(SizeRule::Fixed(300));
    grid.SetWidth(SizeRule::Fixed(500));
    grid.SetModel(model);
    ui.Prepare(700, 650);
    Check(grid.GetChildren().empty(), "Ten thousand property rows allocate no row widgets");
    grid.SelectProperty("property9999", true);
    ui.Prepare(700, 650);
    auto* editor = dynamic_cast<UnitEditor*>(grid.GetActiveEditor());
    Check(editor && grid.GetChildren().size() == 1 && grid.GetScrollOffset().m_Y > 0,
          "One editor is instantiated and the selected virtual row is revealed");
    editor->SetValue(4);
    Check(std::get<double>(model->GetValue("property9999")) == 4,
          "Typed editor commits through canonical property State");
    const auto policy = model->Values().AddValidator(
        [&](const auto& values)
        {
            if (std::get<double>(values.back()) > 5)
            {
                throw std::invalid_argument("Application range");
            }
        });
    editor->SetValue(7);
    Check(std::get<double>(model->GetValue("property9999")) == 4 && editor->GetValue() == 4 &&
              !grid.GetEditError().empty(),
          "Application validation rejects and restores editor values coherently");
    model->SetValue("property9999", 3.0);
    Check(editor->GetValue() == 3, "External canonical commits update the active editor");
    Throws([&] { model->SetValue("property0", std::string("wrong type")); },
           "Wrong property representations reject before commit");
    grid.SetCategoryExpanded("Data", false);
    Check(!grid.GetActiveEditor() && grid.GetChildren().empty(),
          "Collapsing retires the editor without discarding model values");
    Check(std::get<double>(model->GetValue("property9999")) == 3, "Collapsed properties retain canonical data");
    bool rejectedThread = false;
    std::thread worker(
        [&]
        {
            try
            {
                (void)model->GetValue("property0");
            }
            catch (const std::logic_error&)
            {
                rejectedThread = true;
            }
        });
    worker.join();
    Check(rejectedThread, "Property models preserve owner-thread affinity");
    auto& semantics = ui.Semantics();
    const auto gridId = semantics.GetId(grid);
    const auto last = semantics.GetGridItem(gridId, 9999, 1);
    Check(semantics.GetChildCount(gridId) == 10000 && semantics.GetNode(last)->m_Info.m_Number == 3,
          "Virtual semantics page canonical inspector rows without allocating widgets");
    Check(semantics.Perform(last, {SemanticAction::SetNumber, {}, 4}) == SemanticStatus::Succeeded &&
              std::get<double>(model->GetValue("property9999")) == 4,
          "Accessibility numeric edits share canonical property validation");
    Check(semantics.Perform(last, {SemanticAction::SetNumber, {}, 7}) == SemanticStatus::InvalidValue,
          "Accessibility edits reject application-invalid candidates");
    auto replacement = std::make_shared<PropertyModel>(
        ui.States(), std::vector<PropertyDefinition>{{"new", "New property", "General", PropertyKind::Boolean, true}});
    grid.SetModel(replacement);
    Check(!semantics.GetNode(last), "Schema replacement expires old virtual property identities");
    policy.Disconnect();
}
} // namespace

int main()
{
    try
    {
        EStudioUI::FontFace font(ESTUDIOUI_FONT_PATH);
        EStudioUI::UiContext ui(font);
        Units(ui);
        VectorsAndColors(ui);
        Inspector(ui);
        std::cout << "Property editing: " << s_Checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
