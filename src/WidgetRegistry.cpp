#include <EStudioUI/WidgetRegistry.h>
#include <EStudioUI/Widget.h>
#include <EStudioUI/Label.h>
#include <EStudioUI/Button.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/SplitToolButton.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/Slider.h>
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/RadioButton.h>
#include <EStudioUI/ComboBox.h>
#include <EStudioUI/ProgressBar.h>
#include <EStudioUI/BusyIndicator.h>
#include <EStudioUI/Frame.h>
#include <EStudioUI/Separator.h>
#include <EStudioUI/GroupBox.h>
#include <EStudioUI/CollapsiblePanel.h>
#include <EStudioUI/ScrollArea.h>
#include <EStudioUI/ScrollBar.h>
#include <EStudioUI/Splitter.h>
#include <EStudioUI/StackedWidget.h>
#include <EStudioUI/TabWidget.h>
#include <EStudioUI/ToolBar.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/TreeWidget.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/ImageView.h>
#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/SketchWidget.h>
#include <EStudioUI/OpenGLWidget.h>
#include <EStudioUI/PaintWidget.h>
#include <EStudioUI/FilePathEditor.h>
#include <EStudioUI/FolderPicker.h>
#include <EStudioUI/StatusBar.h>
#include <EStudioUI/PropertyGrid.h>
#include <EStudioUI/ColorEditor.h>
#include <EStudioUI/ColorPicker.h>
#include <EStudioUI/UnitEditor.h>
#include <EStudioUI/VectorEditor.h>
#include <EStudioUI/MenuBar.h>

#include <cmath>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{

std::string Number(double value)
{
    if (!std::isfinite(value))
    {
        throw std::invalid_argument("C++ generation requires finite numbers.");
    }
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(17) << value;
    auto result = output.str();
    if (result.find_first_of(".eE") == std::string::npos)
    {
        result += ".0";
    }
    return result;
}

UiPropertyDescriptor Boolean(std::string id, std::string label, bool initial, std::function<void(Widget&, bool)> apply,
                             std::string setter)
{
    UiPropertyDescriptor property;
    property.m_Definition = {std::move(id), std::move(label), "Widget", PropertyKind::Boolean, initial};
    property.m_Apply = [apply](Widget& widget, const PropertyValue& value) { apply(widget, std::get<bool>(value)); };
    property.m_EmitCpp = [setter](std::string_view variable, const PropertyValue& value)
    { return std::string(variable) + "." + setter + "(" + UiCppValue(value) + ");"; };
    return property;
}

UiPropertyDescriptor Text(std::string id, std::string label, std::string initial,
                          std::function<void(Widget&, const std::string&)> apply, std::string setter)
{
    UiPropertyDescriptor property;
    property.m_Definition = {std::move(id), std::move(label), "Widget", PropertyKind::String, std::move(initial)};
    property.m_Apply = [apply](Widget& widget, const PropertyValue& value)
    { apply(widget, std::get<std::string>(value)); };
    property.m_EmitCpp = [setter](std::string_view variable, const PropertyValue& value)
    { return std::string(variable) + "." + setter + "(" + UiCppValue(value) + ");"; };
    return property;
}

UiPropertyDescriptor Scalar(std::string id, std::string label, double initial, double minimum, double maximum,
                            std::function<void(Widget&, double)> apply, std::string setter)
{
    UiPropertyDescriptor property;
    property.m_Definition = {std::move(id), std::move(label), "Widget", PropertyKind::Number, initial};
    property.m_Definition.m_Minimum = minimum;
    property.m_Definition.m_Maximum = maximum;
    property.m_Apply = [apply](Widget& widget, const PropertyValue& value) { apply(widget, std::get<double>(value)); };
    property.m_EmitCpp = [setter](std::string_view variable, const PropertyValue& value)
    { return std::string(variable) + "." + setter + "(" + UiCppValue(value) + ");"; };
    return property;
}

template <typename T> UiWidgetDescriptor Type(std::string typeName, std::string category, bool container = false)
{
    UiWidgetDescriptor descriptor;
    descriptor.m_Type = typeName;
    descriptor.m_Category = std::move(category);
    descriptor.m_CppType = typeName;
    descriptor.m_Include = "EStudioUI/" + typeName + ".h";
    descriptor.m_Create = [] { return std::make_unique<T>(); };
    if (container)
    {
        descriptor.m_Content = [](Widget& widget) -> Widget& { return widget; };
        descriptor.m_CppContent = "$";
    }
    descriptor.m_Properties.push_back(Boolean(
        "enabled", "Enabled", true, [](Widget& widget, bool value) { widget.SetEnabled(value); }, "SetEnabled"));
    descriptor.m_Properties.push_back(Boolean(
        "visible", "Visible", true, [](Widget& widget, bool value) { widget.SetVisible(value); }, "SetVisible"));
    descriptor.m_Properties.push_back(Text(
        "toolTip", "Tool tip", {}, [](Widget& widget, const std::string& value) { widget.SetToolTip(value); },
        "SetToolTip"));
    for (const auto& [id, label] : {std::pair{"width", "Width"}, std::pair{"height", "Height"}})
    {
        const bool width = std::string_view(id) == "width";
        auto property = Scalar(
            id, label, width ? 160 : 36, 0, 100000,
            [width](Widget& widget, double value)
            {
                if (width)
                {
                    widget.SetWidth(SizeRule::Fixed(static_cast<float>(value)));
                }
                else
                {
                    widget.SetHeight(SizeRule::Fixed(static_cast<float>(value)));
                }
            },
            "");
        property.m_Definition.m_Category = "Geometry";
        property.m_Definition.m_Units = {{"px", "length", 1, 0}, {"pt", "length", 96.0 / 72.0, 0}};
        property.m_EmitCpp = [width](std::string_view variable, const PropertyValue& value)
        {
            return std::string(variable) + (width ? ".SetWidth(" : ".SetHeight(") +
                   "SizeRule::Fixed(static_cast<float>(" + UiCppValue(value) + ")));";
        };
        descriptor.m_Properties.push_back(std::move(property));
    }
    for (const auto& name : {std::string("widthPolicy"), std::string("heightPolicy")})
    {
        UiPropertyDescriptor property;
        property.m_Definition = {name, name == "widthPolicy" ? "Width policy" : "Height policy", "Geometry",
                                 PropertyKind::Choice, std::string("Content")};
        property.m_Definition.m_Choices = {"Content", "Fill", "Fixed"};
        // The document builder applies size policies after the scalar dimensions.
        property.m_Apply = [](Widget&, const PropertyValue&) {};
        property.m_EmitCpp = [](std::string_view, const PropertyValue&) { return std::string{}; };
        descriptor.m_Properties.push_back(std::move(property));
    }
    const auto style = [&](std::string name, StyleProperty key, PropertyValue initial, double minimum, double maximum)
    {
        UiPropertyDescriptor property;
        const bool color = std::holds_alternative<Color>(initial);
        property.m_Definition = {"style." + name, name, "CSS", color ? PropertyKind::Color : PropertyKind::Number,
                                 initial};
        property.m_Definition.m_Minimum = minimum;
        property.m_Definition.m_Maximum = maximum;
        property.m_Apply = [key, color](Widget& widget, const PropertyValue& value)
        {
            if (color)
            {
                widget.SetStyleProperty(key, std::get<Color>(value));
            }
            else
            {
                widget.SetStyleProperty(key, static_cast<float>(std::get<double>(value)));
            }
        };
        const std::string enumName = key == StyleProperty::Background    ? "Background"
                                     : key == StyleProperty::Color       ? "Color"
                                     : key == StyleProperty::BorderColor ? "BorderColor"
                                     : key == StyleProperty::Radius      ? "Radius"
                                     : key == StyleProperty::BorderWidth ? "BorderWidth"
                                     : key == StyleProperty::Opacity     ? "Opacity"
                                                                         : "FontSize";
        property.m_EmitCpp = [enumName, color](std::string_view variable, const PropertyValue& value)
        {
            const auto literal = UiCppValue(value);
            return std::string(variable) + ".SetStyleProperty(StyleProperty::" + enumName + ", " +
                   (color ? literal : "static_cast<float>(" + literal + ")") + ");";
        };
        descriptor.m_Properties.push_back(std::move(property));
    };
    style("background", StyleProperty::Background, Color{0.12f, 0.15f, 0.22f, 1}, 0, 1);
    style("color", StyleProperty::Color, Color{0.9f, 0.93f, 1, 1}, 0, 1);
    style("border-color", StyleProperty::BorderColor, Color{0.25f, 0.32f, 0.45f, 1}, 0, 1);
    style("border-radius", StyleProperty::Radius, 6.0, 0, 10000);
    style("border-width", StyleProperty::BorderWidth, 1.0, 0, 1000);
    style("opacity", StyleProperty::Opacity, 1.0, 0, 1);
    style("font-size", StyleProperty::FontSize, 14.0, 1, 512);
    if constexpr (std::is_base_of_v<Label, T>)
    {
        descriptor.m_Properties.push_back(Text(
            "text", "Text", typeName,
            [](Widget& widget, const std::string& value) { static_cast<T&>(widget).SetText(value); }, "SetText"));
    }
    if constexpr (std::is_base_of_v<TextBox, T>)
    {
        descriptor.m_Properties.push_back(Text(
            "text", "Text", {},
            [](Widget& widget, const std::string& value) { static_cast<T&>(widget).SetText(value); }, "SetText"));
        descriptor.m_Properties.push_back(Text(
            "placeholder", "Placeholder", "Enter text", [](Widget& widget, const std::string& value)
            { static_cast<T&>(widget).SetPlaceholder(value); }, "SetPlaceholder"));
        descriptor.m_Properties.push_back(Boolean(
            "readOnly", "Read only", false,
            [](Widget& widget, bool value) { static_cast<T&>(widget).SetReadOnly(value); }, "SetReadOnly"));
    }
    if constexpr (std::is_base_of_v<ToolButton, T> || std::is_base_of_v<CheckBox, T>)
    {
        descriptor.m_Properties.push_back(Boolean(
            "checked", "Checked", false, [](Widget& widget, bool value) { static_cast<T&>(widget).SetChecked(value); },
            "SetChecked"));
    }
    if constexpr (std::is_same_v<T, NumericInput> || std::is_same_v<T, Slider> || std::is_same_v<T, ProgressBar> ||
                  std::is_same_v<T, ScrollBar> || std::is_same_v<T, UnitEditor>)
    {
        descriptor.m_Properties.push_back(Scalar(
            "value", "Value", 0, 0, 100, [](Widget& widget, double value) { static_cast<T&>(widget).SetValue(value); },
            "SetValue"));
    }
    if constexpr (std::is_same_v<T, GroupBox> || std::is_same_v<T, CollapsiblePanel>)
    {
        descriptor.m_Content = [](Widget& widget) -> Widget& { return static_cast<T&>(widget).Content(); };
        descriptor.m_CppContent = "$.Content()";
        descriptor.m_Properties.push_back(Text(
            "title", "Title", typeName,
            [](Widget& widget, const std::string& value) { static_cast<T&>(widget).SetTitle(value); }, "SetTitle"));
    }
    if constexpr (std::is_same_v<T, CollapsiblePanel>)
    {
        descriptor.m_Properties.push_back(Boolean(
            "expanded", "Expanded", true,
            [](Widget& widget, bool value) { static_cast<T&>(widget).SetExpanded(value); }, "SetExpanded"));
    }
    if constexpr (std::is_same_v<T, ScrollArea> || std::is_same_v<T, ToolBar>)
    {
        descriptor.m_Content = [](Widget& widget) -> Widget& { return static_cast<T&>(widget).Content(); };
        descriptor.m_CppContent = "$.Content()";
    }
    if constexpr (std::is_same_v<T, TabWidget>)
    {
        descriptor.m_Content = [](Widget& widget) -> Widget& { return static_cast<T&>(widget).AddTab("Page 1"); };
        descriptor.m_CppContent = "$.AddTab(\"Page 1\")";
    }
    if constexpr (std::is_same_v<T, StackedWidget>)
    {
        descriptor.m_Content = [](Widget& widget) -> Widget& { return static_cast<T&>(widget).AddPage(); };
        descriptor.m_CppContent = "$.AddPage()";
    }
    if constexpr (std::is_same_v<T, ColorEditor> || std::is_same_v<T, ColorPicker>)
    {
        UiPropertyDescriptor property;
        property.m_Definition = {"color", "Color", "Value", PropertyKind::Color, Color{0, 0, 0, 1}};
        property.m_Apply = [](Widget& widget, const PropertyValue& value)
        { static_cast<T&>(widget).SetColor(std::get<Color>(value)); };
        property.m_EmitCpp = [](std::string_view variable, const PropertyValue& value)
        { return std::string(variable) + ".SetColor(" + UiCppValue(value) + ");"; };
        descriptor.m_Properties.push_back(std::move(property));
    }
    if constexpr (std::is_same_v<T, VectorEditor>)
    {
        UiPropertyDescriptor property;
        property.m_Definition = {"value", "Vector", "Value", PropertyKind::Vector, VectorValue{0, 0, 0}};
        property.m_Apply = [](Widget& widget, const PropertyValue& value)
        { static_cast<T&>(widget).SetValue(std::get<VectorValue>(value)); };
        property.m_EmitCpp = [](std::string_view variable, const PropertyValue& value)
        { return std::string(variable) + ".SetValue(" + UiCppValue(value) + ");"; };
        descriptor.m_Properties.push_back(std::move(property));
    }
    return descriptor;
}
} // namespace

std::string UiCppString(std::string_view text)
{
    std::string output = "\"";
    for (const unsigned char byte : text)
    {
        if (byte == '"' || byte == '\\')
        {
            output += '\\';
            output += static_cast<char>(byte);
        }
        else if (byte < 32 || byte >= 127)
        {
            output += '\\';
            output += static_cast<char>('0' + (byte >> 6));
            output += static_cast<char>('0' + ((byte >> 3) & 7));
            output += static_cast<char>('0' + (byte & 7));
        }
        else
        {
            output += static_cast<char>(byte);
        }
    }
    return output + '"';
}

std::string UiCppValue(const PropertyValue& value)
{
    return std::visit(
        [](const auto& data) -> std::string
        {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, bool>)
            {
                return data ? "true" : "false";
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                return Number(data);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                return UiCppString(data);
            }
            else if constexpr (std::is_same_v<T, Color>)
            {
                ValidateColor(data);
                return "Color{" + Number(data.m_R) + "f, " + Number(data.m_G) + "f, " + Number(data.m_B) + "f, " +
                       Number(data.m_A) + "f}";
            }
            else
            {
                std::string output = "VectorValue{";
                for (std::size_t index = 0; index < data.size(); ++index)
                {
                    output += (index ? ", " : "") + Number(data[index]);
                }
                return output + '}';
            }
        },
        value);
}

WidgetRegistry::WidgetRegistry()
{
    Register(Type<Widget>("Widget", "Containers", true));
    Register(Type<Label>("Label", "Controls", false));
    Register(Type<Button>("Button", "Controls", false));
    Register(Type<ToolButton>("ToolButton", "Controls", false));
    Register(Type<SplitToolButton>("SplitToolButton", "Controls", false));
    Register(Type<TextBox>("TextBox", "Controls", false));
    Register(Type<NumericInput>("NumericInput", "Controls", false));
    Register(Type<Slider>("Slider", "Controls", false));
    Register(Type<CheckBox>("CheckBox", "Controls", false));
    Register(Type<RadioButton>("RadioButton", "Controls", false));
    Register(Type<ComboBox>("ComboBox", "Controls", false));
    Register(Type<ProgressBar>("ProgressBar", "Controls", false));
    Register(Type<BusyIndicator>("BusyIndicator", "Controls", false));
    Register(Type<Frame>("Frame", "Containers", true));
    Register(Type<Separator>("Separator", "Controls", false));
    Register(Type<GroupBox>("GroupBox", "Containers", true));
    Register(Type<CollapsiblePanel>("CollapsiblePanel", "Containers", true));
    Register(Type<ScrollArea>("ScrollArea", "Containers", true));
    Register(Type<ScrollBar>("ScrollBar", "Controls", false));
    Register(Type<Splitter>("Splitter", "Containers", true));
    Register(Type<StackedWidget>("StackedWidget", "Containers", true));
    Register(Type<TabWidget>("TabWidget", "Containers", true));
    Register(Type<ToolBar>("ToolBar", "Containers", true));
    Register(Type<ListWidget>("ListWidget", "Data and canvas", false));
    Register(Type<TreeWidget>("TreeWidget", "Data and canvas", false));
    Register(Type<TableWidget>("TableWidget", "Data and canvas", false));
    Register(Type<ImageView>("ImageView", "Data and canvas", false));
    Register(Type<CanvasWidget>("CanvasWidget", "Data and canvas", false));
    Register(Type<SketchWidget>("SketchWidget", "Data and canvas", false));
    Register(Type<OpenGLWidget>("OpenGLWidget", "Data and canvas", false));
    Register(Type<PaintWidget>("PaintWidget", "Data and canvas", false));
    Register(Type<FilePathEditor>("FilePathEditor", "Controls", false));
    Register(Type<FolderPicker>("FolderPicker", "Controls", false));
    Register(Type<StatusBar>("StatusBar", "Controls", false));
    Register(Type<PropertyGrid>("PropertyGrid", "Property editors", false));
    Register(Type<ColorEditor>("ColorEditor", "Property editors", false));
    Register(Type<ColorPicker>("ColorPicker", "Property editors", false));
    Register(Type<UnitEditor>("UnitEditor", "Property editors", false));
    Register(Type<VectorEditor>("VectorEditor", "Property editors", false));
    Register(Type<MenuBar>("MenuBar", "Controls", false));
}

void WidgetRegistry::Register(UiWidgetDescriptor descriptor)
{
    if (m_Types.size() >= 512 || descriptor.m_Type.empty() || descriptor.m_Type.size() > 128 || !descriptor.m_Create ||
        descriptor.m_CppType.empty() || descriptor.m_Include.empty() ||
        (static_cast<bool>(descriptor.m_Content) != !descriptor.m_CppContent.empty()) ||
        descriptor.m_Properties.size() > 256)
    {
        throw std::invalid_argument("Incomplete widget adapter or registry budget exceeded.");
    }
    std::set<std::string> ids;
    for (const auto& property : descriptor.m_Properties)
    {
        if (!ids.insert(property.m_Definition.m_Id).second || property.m_Definition.m_Id.empty() || !property.m_Apply ||
            !property.m_EmitCpp)
        {
            throw std::invalid_argument("Widget adapters require unique complete property adapters.");
        }
        ValidatePropertyDefinition(property.m_Definition);
    }
    if (m_Types.contains(descriptor.m_Type))
    {
        throw std::invalid_argument("Duplicate widget adapter: " + descriptor.m_Type);
    }
    const auto type = descriptor.m_Type;
    m_Types.emplace(type, std::move(descriptor));
}

const std::map<std::string, UiWidgetDescriptor>& WidgetRegistry::GetTypes() const
{
    return m_Types;
}

const UiWidgetDescriptor& WidgetRegistry::Find(std::string_view type) const
{
    const auto found = m_Types.find(std::string(type));
    if (found == m_Types.end())
    {
        throw std::invalid_argument("No widget adapter registered for '" + std::string(type) + "'.");
    }
    return found->second;
}

const UiPropertyDescriptor& WidgetRegistry::FindProperty(std::string_view type, std::string_view property) const
{
    const auto& descriptor = Find(type);
    for (const auto& entry : descriptor.m_Properties)
    {
        if (entry.m_Definition.m_Id == property)
        {
            return entry;
        }
    }
    throw std::invalid_argument("Unknown property '" + std::string(property) + "' on '" + std::string(type) + "'.");
}
} // namespace EStudioUI
