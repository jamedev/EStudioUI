#include <EStudioUI/UiDocument.h>
#include <EStudioUI/CanvasWidget.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/BoxLayout.h>
#include <EStudioUI/ConstraintItem.h>
#include <EStudioUI/ConstraintLayout.h>
#include <EStudioUI/FlowLayout.h>
#include <EStudioUI/GridLayout.h>
#include <EStudioUI/Style.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
using Json = nlohmann::ordered_json;
constexpr std::size_t s_MaxBytes = 4 * 1024 * 1024;

bool Identifier(std::string_view text)
{
    if (text.empty() || text.size() > 128 || text == "parent")
    {
        return false;
    }
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const char character = text[index];
        const bool letter =
            (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || character == '_';
        if (!letter && (index == 0 || !((character >= '0' && character <= '9') || character == '-')))
        {
            return false;
        }
    }
    return true;
}

void Keys(const Json& value, std::initializer_list<std::string_view> allowed)
{
    if (!value.is_object())
    {
        throw std::invalid_argument("Expected a JSON object.");
    }
    for (const auto& [key, entry] : value.items())
    {
        (void)entry;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
        {
            throw std::invalid_argument("Unknown UI document field: " + key);
        }
    }
}

bool AnchorName(std::string_view name)
{
    return name == "Left" || name == "Right" || name == "Top" || name == "Bottom" || name == "CenterX" ||
           name == "CenterY" || name == "Width" || name == "Height";
}

ConstraintExpression Anchor(const ConstraintItem& item, std::string_view name)
{
    if (name == "Left")
    {
        return item.Left();
    }
    if (name == "Right")
    {
        return item.Right();
    }
    if (name == "Top")
    {
        return item.Top();
    }
    if (name == "Bottom")
    {
        return item.Bottom();
    }
    if (name == "CenterX")
    {
        return item.CenterX();
    }
    if (name == "CenterY")
    {
        return item.CenterY();
    }
    if (name == "Width")
    {
        return item.Width();
    }
    if (name == "Height")
    {
        return item.Height();
    }
    throw std::invalid_argument("Unknown constraint anchor.");
}

const char* RelationName(ConstraintRelation relation)
{
    switch (relation)
    {
    case ConstraintRelation::Equal:
        return "Equal";
    case ConstraintRelation::LessEqual:
        return "LessEqual";
    case ConstraintRelation::GreaterEqual:
        return "GreaterEqual";
    }
    throw std::invalid_argument("Unknown constraint relation.");
}

const char* StrengthName(ConstraintStrength strength)
{
    switch (strength)
    {
    case ConstraintStrength::Required:
        return "Required";
    case ConstraintStrength::Strong:
        return "Strong";
    case ConstraintStrength::Medium:
        return "Medium";
    case ConstraintStrength::Weak:
        return "Weak";
    }
    throw std::invalid_argument("Unknown constraint strength.");
}

ConstraintRelation ParseRelation(std::string_view text)
{
    if (text == "Equal")
    {
        return ConstraintRelation::Equal;
    }
    if (text == "LessEqual")
    {
        return ConstraintRelation::LessEqual;
    }
    if (text == "GreaterEqual")
    {
        return ConstraintRelation::GreaterEqual;
    }
    throw std::invalid_argument("Unknown constraint relation.");
}

ConstraintStrength ParseStrength(std::string_view text)
{
    if (text == "Required")
    {
        return ConstraintStrength::Required;
    }
    if (text == "Strong")
    {
        return ConstraintStrength::Strong;
    }
    if (text == "Medium")
    {
        return ConstraintStrength::Medium;
    }
    if (text == "Weak")
    {
        return ConstraintStrength::Weak;
    }
    throw std::invalid_argument("Unknown constraint strength.");
}

Json WriteValue(const PropertyValue& value)
{
    return std::visit(
        [](const auto& data) -> Json
        {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, Color>)
            {
                return Json::array({data.m_R, data.m_G, data.m_B, data.m_A});
            }
            else
            {
                return Json(data);
            }
        },
        value);
}

PropertyValue ReadValue(const Json& value, const PropertyDefinition& definition)
{
    switch (definition.m_Kind)
    {
    case PropertyKind::Boolean:
        return value.get<bool>();
    case PropertyKind::Number:
        return value.get<double>();
    case PropertyKind::String:
    case PropertyKind::Choice:
        return value.get<std::string>();
    case PropertyKind::Color:
        if (!value.is_array() || value.size() != 4)
        {
            throw std::invalid_argument("A JSON color requires four normalized channels.");
        }
        return Color{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
    case PropertyKind::Vector:
        return value.get<VectorValue>();
    }
    throw std::invalid_argument("Unknown property kind.");
}

Json WriteNode(const UiNode& node)
{
    Json result{{"id", node.m_Id},
                {"type", node.m_Type},
                {"layout", node.m_Layout},
                {"columns", node.m_Columns},
                {"gap", node.m_Gap}};
    result["properties"] = Json::object();
    for (const auto& [name, value] : node.m_Properties)
    {
        result["properties"][name] = WriteValue(value);
    }
    result["children"] = Json::array();
    for (const auto& child : node.m_Children)
    {
        result["children"].push_back(WriteNode(child));
    }
    result["constraints"] = Json::array();
    for (const auto& rule : node.m_Constraints)
    {
        result["constraints"].push_back({{"item", rule.m_Item},
                                         {"anchor", rule.m_Anchor},
                                         {"relation", RelationName(rule.m_Relation)},
                                         {"target", rule.m_Target},
                                         {"targetAnchor", rule.m_TargetAnchor},
                                         {"multiplier", rule.m_Multiplier},
                                         {"offset", rule.m_Offset},
                                         {"strength", StrengthName(rule.m_Strength)},
                                         {"name", rule.m_Name}});
    }
    return result;
}

UiNode ReadNode(const Json& value, const WidgetRegistry& registry, std::size_t depth, std::size_t& count)
{
    if (depth > 32 || ++count > 4096)
    {
        throw std::length_error("UI documents support at most 4,096 nodes and 32 widget levels.");
    }
    Keys(value, {"id", "type", "properties", "layout", "columns", "gap", "children", "constraints"});
    UiNode node;
    node.m_Id = value.at("id").get<std::string>();
    node.m_Type = value.at("type").get<std::string>();
    node.m_Layout = value.value("layout", std::string("Column"));
    if (value.contains("columns") && !value["columns"].is_number_integer())
    {
        throw std::invalid_argument("Grid column counts must be JSON integers.");
    }
    node.m_Columns = value.value("columns", 2);
    node.m_Gap = value.value("gap", 8.0f);
    if (value.contains("properties"))
    {
        if (!value["properties"].is_object() || value["properties"].size() > 256)
        {
            throw std::invalid_argument("Properties must be a bounded JSON object.");
        }
        for (const auto& [name, property] : value["properties"].items())
        {
            node.m_Properties.emplace(name, ReadValue(property, registry.FindProperty(node.m_Type, name).m_Definition));
        }
    }
    if (value.contains("children"))
    {
        if (!value["children"].is_array() || value["children"].size() > 4096)
        {
            throw std::invalid_argument("Children must be a bounded JSON array.");
        }
        for (const auto& child : value["children"])
        {
            node.m_Children.push_back(ReadNode(child, registry, depth + 1, count));
        }
    }
    if (value.contains("constraints"))
    {
        if (!value["constraints"].is_array() || value["constraints"].size() > 1024)
        {
            throw std::invalid_argument("Constraints must be a bounded JSON array.");
        }
        for (const auto& rule : value["constraints"])
        {
            Keys(rule,
                 {"item", "anchor", "relation", "target", "targetAnchor", "multiplier", "offset", "strength", "name"});
            UiConstraint constraint;
            constraint.m_Item = rule.at("item").get<std::string>();
            constraint.m_Anchor = rule.at("anchor").get<std::string>();
            constraint.m_Relation = ParseRelation(rule.value("relation", std::string("Equal")));
            constraint.m_Target = rule.value("target", std::string("parent"));
            constraint.m_TargetAnchor = rule.value("targetAnchor", std::string("Left"));
            constraint.m_Multiplier = rule.value("multiplier", 1.0);
            constraint.m_Offset = rule.value("offset", 0.0);
            constraint.m_Strength = ParseStrength(rule.value("strength", std::string("Required")));
            constraint.m_Name = rule.value("name", std::string{});
            node.m_Constraints.push_back(std::move(constraint));
        }
    }
    return node;
}

void ValidateNode(const UiNode& node, const WidgetRegistry& registry, std::set<std::string>& ids, std::size_t depth,
                  std::size_t& count, std::size_t& bytes)
{
    if (depth > 32 || ++count > 4096)
    {
        throw std::length_error("UI documents support at most 4,096 nodes and 32 widget levels.");
    }
    bytes += sizeof(UiNode) + node.m_Id.size() + node.m_Type.size();
    for (const auto& [name, value] : node.m_Properties)
    {
        bytes += name.size() + sizeof(PropertyValue);
        if (const auto* text = std::get_if<std::string>(&value))
        {
            bytes += text->size();
        }
        if (const auto* vector = std::get_if<VectorValue>(&value))
        {
            bytes += vector->size() * sizeof(double);
        }
    }
    for (const auto& rule : node.m_Constraints)
    {
        bytes += sizeof(UiConstraint) + rule.m_Item.size() + rule.m_Target.size() + rule.m_Name.size() +
                 rule.m_Anchor.size() + rule.m_TargetAnchor.size();
    }
    if (bytes > s_MaxBytes)
    {
        throw std::length_error("UI document value storage exceeds 4 MiB.");
    }
    const auto& descriptor = registry.Find(node.m_Type);
    if (!Identifier(node.m_Id) || !ids.insert(node.m_Id).second)
    {
        throw std::invalid_argument("Every design node requires a distinct CSS-safe ID; 'parent' is reserved.");
    }
    if (node.m_Properties.size() > 256 || node.m_Columns < 1 || node.m_Columns > 64 || !std::isfinite(node.m_Gap) ||
        node.m_Gap < 0 || node.m_Gap > 10000 || node.m_Constraints.size() > 1024)
    {
        throw std::invalid_argument("Invalid node geometry or property/constraint budget.");
    }
    if (node.m_Layout != "Column" && node.m_Layout != "Row" && node.m_Layout != "Grid" && node.m_Layout != "Flow" &&
        node.m_Layout != "Constraints")
    {
        throw std::invalid_argument("Unknown UI layout: " + node.m_Layout);
    }
    if (!descriptor.m_Content && !node.m_Children.empty())
    {
        throw std::invalid_argument("Widget '" + node.m_Type + "' has no registered content slot.");
    }
    for (const auto& [name, value] : node.m_Properties)
    {
        ValidatePropertyValue(registry.FindProperty(node.m_Type, name).m_Definition, value);
    }
    std::set<std::string> children;
    for (const auto& child : node.m_Children)
    {
        children.insert(child.m_Id);
    }
    for (const auto& rule : node.m_Constraints)
    {
        if (node.m_Layout != "Constraints" || !children.contains(rule.m_Item) ||
            (rule.m_Target != "parent" && !children.contains(rule.m_Target)) || !AnchorName(rule.m_Anchor) ||
            !AnchorName(rule.m_TargetAnchor) || !std::isfinite(rule.m_Multiplier) ||
            std::abs(rule.m_Multiplier) > 1e6 || !std::isfinite(rule.m_Offset) || std::abs(rule.m_Offset) > 1e6 ||
            rule.m_Name.size() > 256)
        {
            throw std::invalid_argument("Invalid constraint or reference outside immediate siblings.");
        }
        (void)RelationName(rule.m_Relation);
        (void)StrengthName(rule.m_Strength);
    }
    for (const auto& child : node.m_Children)
    {
        ValidateNode(child, registry, ids, depth + 1, count, bytes);
    }
}

void ApplySizing(Widget& widget, const UiNode& node)
{
    for (const bool width : {true, false})
    {
        const auto policy = node.m_Properties.find(width ? "widthPolicy" : "heightPolicy");
        if (policy == node.m_Properties.end())
        {
            continue;
        }
        const auto& name = std::get<std::string>(policy->second);
        if (name == "Fixed")
        {
            const auto size = node.m_Properties.find(width ? "width" : "height");
            const float pixels = size == node.m_Properties.end() ? (width ? 160.0f : 36.0f)
                                                                 : static_cast<float>(std::get<double>(size->second));
            if (width)
            {
                widget.SetWidth(SizeRule::Fixed(pixels));
            }
            else
            {
                widget.SetHeight(SizeRule::Fixed(pixels));
            }
        }
        else
        {
            const auto size = name == "Fill" ? SizeRule::Fill() : SizeRule::Content();
            if (width)
            {
                widget.SetWidth(size);
            }
            else
            {
                widget.SetHeight(size);
            }
        }
    }
}

std::unique_ptr<Widget> Construct(const UiNode& node, const WidgetRegistry& registry, UiBuildResult& result)
{
    const auto& descriptor = registry.Find(node.m_Type);
    auto created = descriptor.m_Create();
    if (!created)
    {
        throw std::runtime_error("Widget adapter returned an empty widget: " + node.m_Type);
    }
    created->SetId(node.m_Id);
    for (const auto& [name, value] : node.m_Properties)
    {
        registry.FindProperty(node.m_Type, name).m_Apply(*created, value);
    }
    ApplySizing(*created, node);
    auto& widget = *created;
    result.m_Widgets.emplace(node.m_Id, WidgetRef(&widget));
    if (!descriptor.m_Content)
    {
        return created;
    }
    auto& content = descriptor.m_Content(widget);
    content.SetGap(node.m_Gap);
    for (const auto& child : node.m_Children)
    {
        content.AddChild(Construct(child, registry, result));
    }
    if (node.m_Layout == "Constraints")
    {
        ConstraintLayout layout;
        for (const auto& rule : node.m_Constraints)
        {
            const auto* left = result.m_Widgets.at(rule.m_Item).Get();
            const auto lhs = Anchor(ConstraintItem(*left), rule.m_Anchor);
            const auto rhs =
                Anchor(rule.m_Target == "parent" ? ConstraintItem::Parent()
                                                 : ConstraintItem(*result.m_Widgets.at(rule.m_Target).Get()),
                       rule.m_TargetAnchor) *
                    rule.m_Multiplier +
                rule.m_Offset;
            layout.Add(Constraint(lhs - rhs, rule.m_Relation), rule.m_Strength, rule.m_Name);
        }
        content.SetLayout(std::move(layout));
    }
    else if (node.m_Layout == "Grid")
    {
        content.SetLayout(
            GridLayout(std::vector<SizeRule>(static_cast<std::size_t>(node.m_Columns), SizeRule::Fill())));
    }
    else if (node.m_Layout == "Flow")
    {
        content.SetLayout(FlowLayout{});
    }
    else
    {
        content.SetLayout(BoxLayout(node.m_Layout == "Row" ? Axis::Horizontal : Axis::Vertical));
    }
    return created;
}

void EmitNode(std::ostringstream& output, const UiNode& node, std::string parent, const WidgetRegistry& registry,
              std::map<std::string, std::string>& variables, std::size_t& serial)
{
    const auto& descriptor = registry.Find(node.m_Type);
    const std::string variable = "widget" + std::to_string(serial++);
    variables.emplace(node.m_Id, variable);
    output << "    auto& " << variable << " = " << parent << ".Add<" << descriptor.m_CppType << ">();\n"
           << "    " << variable << ".SetId(" << UiCppString(node.m_Id) << ");\n";
    for (const auto& [name, value] : node.m_Properties)
    {
        const auto line = registry.FindProperty(node.m_Type, name).m_EmitCpp(variable, value);
        if (!line.empty())
        {
            output << "    " << line << '\n';
        }
    }
    for (const bool width : {true, false})
    {
        const auto policy = node.m_Properties.find(width ? "widthPolicy" : "heightPolicy");
        if (policy == node.m_Properties.end())
        {
            continue;
        }
        const auto& name = std::get<std::string>(policy->second);
        output << "    " << variable << (width ? ".SetWidth(" : ".SetHeight(") << "SizeRule::" << name << '(';
        if (name == "Fixed")
        {
            const auto value = node.m_Properties.find(width ? "width" : "height");
            output << "static_cast<float>("
                   << UiCppValue(value == node.m_Properties.end() ? PropertyValue(width ? 160.0 : 36.0) : value->second)
                   << ')';
        }
        output << "));\n";
    }
    if (!descriptor.m_Content)
    {
        return;
    }
    const std::string content = "content" + std::to_string(serial++);
    auto expression = descriptor.m_CppContent;
    expression.replace(expression.find('$'), 1, variable);
    // A Widget reference deliberately routes additions to the resolved content slot.
    output << "    Widget& " << content << " = " << expression << ";\n"
           << "    " << content << ".SetGap(static_cast<float>(" << UiCppValue(static_cast<double>(node.m_Gap))
           << "));\n";
    for (const auto& child : node.m_Children)
    {
        EmitNode(output, child, content, registry, variables, serial);
    }
    if (node.m_Layout == "Constraints")
    {
        const auto layout = "layout" + std::to_string(serial++);
        output << "    ConstraintLayout " << layout << ";\n";
        for (const auto& rule : node.m_Constraints)
        {
            const auto lhs = "ConstraintItem(" + variables.at(rule.m_Item) + ")." + rule.m_Anchor + "()";
            const auto rhs = (rule.m_Target == "parent" ? std::string("ConstraintItem::Parent()")
                                                        : "ConstraintItem(" + variables.at(rule.m_Target) + ")") +
                             "." + rule.m_TargetAnchor + "() * " + UiCppValue(rule.m_Multiplier) + " + " +
                             UiCppValue(rule.m_Offset);
            const char* relation = rule.m_Relation == ConstraintRelation::Equal       ? " == "
                                   : rule.m_Relation == ConstraintRelation::LessEqual ? " <= "
                                                                                      : " >= ";
            output << "    " << layout << ".Add(" << lhs << relation << rhs
                   << ", ConstraintStrength::" << StrengthName(rule.m_Strength) << ", " << UiCppString(rule.m_Name)
                   << ");\n";
        }
        output << "    " << content << ".SetLayout(std::move(" << layout << "));\n";
    }
    else if (node.m_Layout == "Grid")
    {
        output << "    " << content << ".SetLayout(GridLayout(std::vector<SizeRule>(" << node.m_Columns
               << ", SizeRule::Fill())));\n";
    }
    else if (node.m_Layout == "Flow")
    {
        output << "    " << content << ".SetLayout(FlowLayout{});\n";
    }
    else
    {
        output << "    " << content
               << ".SetLayout(BoxLayout(Axis::" << (node.m_Layout == "Row" ? "Horizontal" : "Vertical") << "));\n";
    }
}

UiNode* FindNode(UiNode& node, std::string_view id)
{
    if (node.m_Id == id)
    {
        return &node;
    }
    for (auto& child : node.m_Children)
    {
        if (auto* found = FindNode(child, id))
        {
            return found;
        }
    }
    return nullptr;
}
} // namespace

UiNode::UiNode() = default;

UiNode::UiNode(std::string id, std::string type, std::map<std::string, PropertyValue> properties)
    : m_Id(std::move(id)), m_Type(std::move(type)), m_Properties(std::move(properties))
{
}

UiDocument::UiDocument()
{
    m_Root.m_Id = "design-root";
}

void UiDocument::Validate(const WidgetRegistry& registry, bool validateCss) const
{
    if (m_StyleSheet.size() > 262144)
    {
        throw std::length_error("Document CSS is limited to 256 KiB.");
    }
    std::set<std::string> ids;
    std::size_t count = 0;
    std::size_t bytes = m_StyleSheet.size();
    ValidateNode(m_Root, registry, ids, 0, count, bytes);
    if (!validateCss)
    {
        return;
    }
    StyleSheet compiled;
    std::string error;
    if (!compiled.Parse(m_StyleSheet, error))
    {
        throw std::invalid_argument("Document CSS: " + error);
    }
}

UiDocument UiDocument::FromJson(std::string_view json, const WidgetRegistry& registry)
{
    if (json.size() > s_MaxBytes)
    {
        throw std::length_error("UI JSON is limited to 4 MiB.");
    }
    try
    {
        std::map<int, std::set<std::string>> keys;
        const auto parsed = Json::parse(
            json,
            [&keys](int depth, Json::parse_event_t event, Json& value)
            {
                if (depth > 96)
                {
                    throw std::length_error("UI JSON nesting exceeds 96 levels.");
                }
                if (event == Json::parse_event_t::object_start)
                {
                    keys[depth + 1].clear();
                }
                if (event == Json::parse_event_t::key && !keys[depth].insert(value.get<std::string>()).second)
                {
                    throw std::invalid_argument("Duplicate JSON object key: " + value.get<std::string>());
                }
                if (event == Json::parse_event_t::object_end)
                {
                    keys.erase(depth + 1);
                }
                return true;
            });
        Keys(parsed, {"format", "version", "root", "styleSheet"});
        if (parsed.at("format") != "EStudioUI" || !parsed.at("version").is_number_integer() ||
            parsed.at("version") != 1)
        {
            throw std::invalid_argument("Unsupported UI document format or version.");
        }
        UiDocument result;
        result.m_StyleSheet = parsed.value("styleSheet", std::string{});
        std::size_t count = 0;
        result.m_Root = ReadNode(parsed.at("root"), registry, 0, count);
        result.Validate(registry);
        return result;
    }
    catch (const Json::exception& error)
    {
        throw std::invalid_argument(std::string("Invalid UI JSON: ") + error.what());
    }
}

std::string UiDocument::ToJson(const WidgetRegistry& registry) const
{
    Validate(registry);
    auto output =
        Json{{"format", "EStudioUI"}, {"version", 1}, {"styleSheet", m_StyleSheet}, {"root", WriteNode(m_Root)}}.dump(
            2) +
        '\n';
    if (output.size() > s_MaxBytes)
    {
        throw std::length_error("Serialized UI document exceeds 4 MiB.");
    }
    return output;
}

std::string UiDocument::ToCpp(const WidgetRegistry& registry) const
{
    Validate(registry);
    std::set<std::string> includes{"EStudioUI/Widget.h",     "EStudioUI/PropertyValue.h",
                                   "EStudioUI/BoxLayout.h",  "EStudioUI/GridLayout.h",
                                   "EStudioUI/FlowLayout.h", "EStudioUI/ConstraintLayout.h"};
    const auto gather = [&](const auto& self, const UiNode& node) -> void
    {
        includes.insert(registry.Find(node.m_Type).m_Include);
        for (const auto& child : node.m_Children)
        {
            self(self, child);
        }
    };
    gather(gather, m_Root);
    std::ostringstream output;
    output << "// Generated by EStudioUICreator. Ordinary editable C++20; no document loader is required.\n";
    for (const auto& include : includes)
    {
        output << "#include <" << include << ">\n";
    }
    output << "#include <string_view>\n#include <utility>\n#include <vector>\n\n"
           << "// Apply this optional stylesheet to the containing UI explicitly.\n"
           << "std::string_view UiStyleSheet()\n{\n    return " << UiCppString(m_StyleSheet) << ";\n}\n\n"
           << "EStudioUI::Widget& BuildUi(EStudioUI::Widget& parent)\n{\n    using namespace EStudioUI;\n";
    std::map<std::string, std::string> variables;
    std::size_t serial = 0;
    EmitNode(output, m_Root, "parent", registry, variables, serial);
    output << "    return " << variables.at(m_Root.m_Id) << ";\n}\n";
    if (output.str().size() > 16 * 1024 * 1024)
    {
        throw std::length_error("Generated C++ exceeds 16 MiB.");
    }
    return output.str();
}

UiNode* UiDocument::Find(std::string_view id)
{
    return FindNode(m_Root, id);
}

const UiNode* UiDocument::Find(std::string_view id) const
{
    return const_cast<UiDocument*>(this)->Find(id);
}

UiBuildResult BuildUiDocument(Widget& parent, const UiDocument& document, const WidgetRegistry& registry)
{
    parent.VerifyAccess();
    if (parent.GetContext() && parent.GetContext()->GetPhase() != UiPhase::Idle)
    {
        throw std::logic_error("Build a UI document at an idle owner boundary.");
    }
    document.Validate(registry);
    // Build entirely off-tree. Validation/factory failures leave the host unchanged.
    UiBuildResult result;
    auto root = Construct(document.m_Root, registry, result);
    result.m_Root = WidgetRef(root.get());
    parent.AddChild(std::move(root));
    return result;
}

UiBuildResult BuildUiDocument(CanvasWidget& canvas, CanvasBounds bounds, const UiDocument& document,
                              const WidgetRegistry& registry)
{
    canvas.VerifyAccess();
    if (canvas.GetContext() && canvas.GetContext()->GetPhase() != UiPhase::Idle)
    {
        throw std::logic_error("Host a UI document at an idle owner boundary.");
    }
    auto host = std::make_unique<Widget>();
    auto result = BuildUiDocument(*host, document, registry);
    canvas.HostWidget(std::move(host), bounds);
    return result;
}
} // namespace EStudioUI
