#include <EStudioUI/Bindings.h>
#include <EStudioUI/ColorEditor.h>
#include <EStudioUI/CollapsiblePanel.h>
#include <EStudioUI/PropertyGrid.h>
#include <EStudioUI/UnitEditor.h>
#include <EStudioUI/VectorEditor.h>

void BuildPropertyInspector(EStudioUI::UiContext& ui)
{
    using namespace EStudioUI;
    std::vector<PropertyDefinition> properties{
        {"name", "Name", "General", PropertyKind::String, std::string("Part")},
        {"visible", "Visible", "General", PropertyKind::Boolean, true},
        {"position", "Position", "Geometry", PropertyKind::Vector, VectorValue{0, 0, 0}},
        {"color", "Color", "Appearance", PropertyKind::Color, Color{.2f, .6f, 1, 1}}};
    PropertyDefinition length("length", "Length", "Geometry", PropertyKind::Number, .0254);
    length.m_Minimum = 0;
    length.m_Maximum = 100;
    length.m_Units = {{"m", "length", 1, 0}, {"mm", "length", .001, 0}, {"in", "length", .0254, 0}};
    properties.push_back(std::move(length));
    auto model = std::make_shared<PropertyModel>(ui.States(), std::move(properties));
    auto& section = ui.Root().Add<CollapsiblePanel>("Properties");
    auto& inspector = section.Content().Add<PropertyGrid>();
    inspector.SetHeight(SizeRule::Fill());
    inspector.SetModel(model);
    model->SetValue("length", .0508);
}

int main()
{
    return 0;
}
