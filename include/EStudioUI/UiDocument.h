#pragma once

#include "WidgetRegistry.h"
#include "Constraint.h"

#include <unordered_map>

namespace EStudioUI
{
class CanvasWidget;
struct CanvasBounds;

//! One serialized linear relationship between immediate-child or parent anchors.
struct UiConstraint
{
    //! Stable child identity on the left.
    std::string m_Item;

    //! Left, Right, Top, Bottom, CenterX, CenterY, Width or Height.
    std::string m_Anchor = "Left";

    //! Equal, LessEqual or GreaterEqual.
    ConstraintRelation m_Relation = ConstraintRelation::Equal;

    //! Stable sibling identity, or the reserved parent identity.
    std::string m_Target = "parent";

    //! Anchor on the right.
    std::string m_TargetAnchor = "Left";

    //! Finite affine relationship: lhs relation rhs * multiplier + offset.
    double m_Multiplier = 1, m_Offset = 0;

    //! Solver priority.
    ConstraintStrength m_Strength = ConstraintStrength::Required;

    //! Owned diagnostic label.
    std::string m_Name;

    //! Compares serialized rule values.
    bool operator==(const UiConstraint&) const = default;
};

//! A persistent design node, independent of live widgets or renderer resources.
struct UiNode
{
    //! Creates an ordinary empty panel design.
    UiNode();

    //! Creates a named typed node with explicit property overrides.
    UiNode(std::string id, std::string type, std::map<std::string, PropertyValue> properties = {});

    //! Stable unique CSS-safe identity.
    std::string m_Id;

    //! Registry type, never an executable constructor expression.
    std::string m_Type = "Widget";

    //! Explicit property overrides; omitted values retain framework defaults.
    std::map<std::string, PropertyValue> m_Properties;

    //! Column, Row, Grid, Flow or Constraints for the content slot.
    std::string m_Layout = "Column";

    //! Positive grid column count.
    int m_Columns = 2;

    //! Nonnegative logical-pixel layout gap.
    float m_Gap = 8;

    //! Ordered child designs.
    std::vector<UiNode> m_Children;

    //! Optional immediate-child linear anchor rules.
    std::vector<UiConstraint> m_Constraints;

    //! Compares complete design values.
    bool operator==(const UiNode&) const = default;
};

//! A versioned bounded open UI description with separate JSON and direct C++ outputs.
class UiDocument
{
  public:
    //! Creates an empty ordinary root panel.
    UiDocument();

    //! Compares design values independently of live widgets.
    bool operator==(const UiDocument&) const = default;

    //! Owns the root design node.
    UiNode m_Root;

    //! Owns optional ordinary EStudioUI CSS, parsed only when explicitly applied.
    std::string m_StyleSheet;

    //! Validates the whole design before construction or generation.
    void Validate(const WidgetRegistry& registry, bool validateCss = true) const;

    //! Parses version 1 JSON with byte/depth/count/duplicate-key limits before returning a design.
    static UiDocument FromJson(std::string_view json, const WidgetRegistry& registry);

    //! Writes deterministic human-readable version 1 JSON.
    std::string ToJson(const WidgetRegistry& registry) const;

    //! Writes direct editable C++ BuildUi(parent), containing no JSON parser or serialized design.
    std::string ToCpp(const WidgetRegistry& registry) const;

    //! Finds a design node by stable identity; null means absent.
    UiNode* Find(std::string_view id);

    //! Finds a read-only node by stable identity.
    const UiNode* Find(std::string_view id) const;
};

//! Actual widgets constructed from one validated design.
struct UiBuildResult
{
    //! Observes the root whose ownership belongs to the supplied parent.
    WidgetRef m_Root;

    //! Maps stable design identities to lifetime-safe actual widgets.
    std::unordered_map<std::string, WidgetRef> m_Widgets;
};

//! Builds a validated document under any ordinary container, including canvas-hosted widget containers.
UiBuildResult BuildUiDocument(Widget& parent, const UiDocument& document, const WidgetRegistry& registry);
//! Hosts a runtime-built dashboard through the canvas's existing sparse widget mechanism.
UiBuildResult BuildUiDocument(CanvasWidget& canvas, CanvasBounds bounds, const UiDocument& document,
                              const WidgetRegistry& registry);
} // namespace EStudioUI
