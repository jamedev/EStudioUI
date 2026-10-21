#pragma once

#include "Types.h"
#include "VisualStyle.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace EStudioUI
{
//! CSS pseudo-class flags maintained by the input dispatcher.
enum PseudoState : unsigned
{
    Hover = 1,
    Pressed = 2,
    Focus = 4,
    Disabled = 8,
    ReadOnly = 16,
    Selected = 32
};

//! Describes the identity and states visible to CSS selectors.
struct StyleNode
{
    //! Lowercase widget type name.
    std::string m_Type;

    //! Optional case-sensitive CSS identifier.
    std::string m_Id;

    //! Case-sensitive CSS classes.
    std::vector<std::string> m_Classes;

    //! Active pseudo-class flags.
    unsigned m_State = 0;

    //! Non-owning ancestor used by descendant and child selectors.
    const StyleNode* m_Parent = nullptr;
};

//! A CSS dimension: auto, logical pixels, or a parent-relative percentage.
struct Length
{
    //! Supported units.
    enum class Unit
    {
        Auto,
        Pixels,
        Percent,
        Fraction
    };

    //! Dimension unit.
    Unit m_Unit = Unit::Auto;

    //! Numeric value for explicit units.
    float m_Value = 0;

    //! Compares both the unit and numeric value.
    bool operator==(const Length&) const = default;

    //! Resolves a dimension against its parent or intrinsic fallback.
    float Resolve(float parent, float fallback) const;
};

//! Four logical-pixel edge distances in CSS order.
struct Insets
{
    //! Top distance.
    float m_Top = 0;

    //! Right distance.
    float m_Right = 0;

    //! Bottom distance.
    float m_Bottom = 0;

    //! Left distance.
    float m_Left = 0;

    //! Compares all four edges exactly.
    bool operator==(const Insets&) const = default;
};

//! A soft analytic shadow behind a widget's rounded rectangle.
struct Shadow
{
    //! Horizontal offset.
    float m_X = 0;

    //! Vertical offset.
    float m_Y = 0;

    //! Soft edge radius.
    float m_Blur = 0;

    //! Shadow color; zero alpha disables the shadow.
    Color m_Color{0, 0, 0, 0};

    //! Expands the silhouette before applying blur.
    float m_Spread = 0;

    //! Draws the shadow inside the box.
    bool m_Inset = false;

    //! Compares offset, blur, and color exactly.
    bool operator==(const Shadow&) const = default;
};

//! Additional shadows after the first, preserving CSS front-to-back order.
struct ShadowLayers
{
    //! At most seven additional layers.
    std::vector<Shadow> m_Layers;

    //! Compares all layers.
    bool operator==(const ShadowLayers&) const = default;
};

//! Optional percentage or pixel offsets for a decorative border box.
struct Anchors
{
    //! Auto edges resolve from the opposite edge or the owner origin.
    Length m_Top, m_Right, m_Bottom, m_Left;

    //! Compares all edge units and values.
    bool operator==(const Anchors&) const = default;
};

//! Computed presentation and basic flex layout values.
struct Style
{
    //! Fill color.
    Color m_Background{0, 0, 0, 0};

    //! Inherited foreground color.
    Color m_Color{0.9f, 0.92f, 0.96f, 1};

    //! Border color.
    Color m_BorderColor{0, 0, 0, 0};

    //! Text selection background.
    Color m_SelectionBackground{0.2f, 0.4f, 0.85f, 0.75f};

    //! Foreground inside a text selection.
    Color m_SelectionColor{1, 1, 1, 1};

    //! Placeholder foreground.
    Color m_PlaceholderColor{0.45f, 0.5f, 0.6f, 1};

    //! Insertion caret color.
    Color m_CaretColor{0.6f, 0.75f, 1, 1};

    //! Border thickness.
    float m_BorderWidth = 0;

    //! Corner radius.
    float m_Radius = 0;

    //! Inherited font size in logical pixels.
    float m_FontSize = 16;

    //! Local opacity, multiplied by ancestor opacity when painting.
    float m_Opacity = 1;

    //! Interior edge distances.
    Insets m_Padding;

    //! Exterior edge distances.
    Insets m_Margin;

    //! Optional shadow.
    Shadow m_Shadow;

    //! Additional shadow layers after m_Shadow.
    ShadowLayers m_ShadowLayers;

    //! Optional analytic gradient over the background color.
    Gradient m_BackgroundImage;

    //! Ordered visual transform; has no effect on layout allocation.
    Transform m_Transform;

    //! Clips decoration and descendants to the rounded border box.
    bool m_ClipContent = false;

    //! Enables an empty decorative pseudo-element through content: "".
    bool m_Decoration = false;

    //! Decorative box placement relative to the owner border box.
    Anchors m_Anchors;

    //! Compiled automatic transition specifications.
    std::vector<CssTransition> m_Transitions;

    //! Compiled keyframe animation specifications.
    std::vector<CssAnimation> m_Animations;

    //! Explicit or intrinsic width.
    Length m_Width;

    //! Explicit or intrinsic height.
    Length m_Height;

    //! Minimum width.
    float m_MinWidth = 0;

    //! Minimum height.
    float m_MinHeight = 0;

    //! Maximum width.
    float m_MaxWidth = 1000000;

    //! Maximum height.
    float m_MaxHeight = 1000000;

    //! Space between flex children.
    float m_Gap = 0;

    //! Share of free main-axis space.
    float m_FlexGrow = 0;

    //! Whether children flow horizontally.
    bool m_Row = false;

    //! Whether this node participates in layout and input.
    bool m_Displayed = true;

    //! Cross-axis alignment: stretch, start, center, end.
    std::string m_Align = "stretch";

    //! Main-axis alignment: start, center, end, space-between.
    std::string m_Justify = "start";

    //! Text alignment: left, center, right.
    std::string m_TextAlign = "left";

    //! Height of a list/tree/table row in logical pixels.
    float m_RowHeight = 32;

    //! Horizontal tree indentation per nesting level.
    float m_Indent = 20;

    //! Table header and popup separator background.
    Color m_HeaderBackground{0.15f, 0.19f, 0.28f, 1};

    //! Alternating row background.
    Color m_AlternateBackground{0.5f, 0.6f, 0.8f, 0.04f};

    //! Background of a hovered row.
    Color m_HoverBackground{0.5f, 0.6f, 0.9f, 0.14f};

    //! Table grid and scrollbar track color.
    Color m_GridColor{0.4f, 0.5f, 0.7f, 0.2f};

    //! Horizontal translation after layout; flow descendants and hit testing follow.
    float m_TranslateX = 0;

    //! Vertical translation after layout; flow descendants and hit testing follow.
    float m_TranslateY = 0;
};

//! Parses and applies a documented CSS subset with an atomic reload operation.
class StyleSheet
{
  public:
    //! Parses a stylesheet, retaining the previous rules on error.
    bool Parse(std::string_view css, std::string& error);

    //! Reads and parses a UTF-8 CSS file.
    bool LoadFile(const std::filesystem::path& path, std::string& error);

    //! Resolves matching declarations, inheritance, and selector specificity.
    Style Compute(const StyleNode& node, const Style* inherited = nullptr, int pseudoElement = 0) const;

    //! One compiled keyframe block with normalized offsets.
    struct Keyframe
    {
        //! Normalized offset in [0,1].
        double m_Offset = 0;

        //! Applies already parsed values to an underlying style.
        std::function<void(Style&)> m_Apply;

        //! Canonical properties explicitly present in this block.
        std::vector<std::string> m_Properties;

        //! Optional easing for the following interval.
        std::optional<EasingCurve> m_Easing;
    };

    //! Returns compiled blocks for a case-sensitive animation name.
    const std::vector<Keyframe>* FindKeyframes(std::string_view name) const;

    //! Returns the current stylesheet revision for invalidation.
    unsigned GetRevision() const;

  private:
    //! A compound selector such as Button.primary:hover.
    struct SelectorPart
    {
        //! Optional type selector.
        std::string m_Type;

        //! Optional identifier selector.
        std::string m_Id;

        //! Required class selectors.
        std::vector<std::string> m_Classes;

        //! Required pseudo-class states.
        unsigned m_Required = 0;

        //! Forbidden pseudo-class states.
        unsigned m_Forbidden = 0;

        //! Whether the preceding part must be the immediate parent.
        bool m_Child = false;
    };

    //! One validated declaration.
    struct Declaration
    {
        //! Lowercase property name.
        std::string m_Name;

        //! Property value before application.
        std::string m_Value;

        //! Typed assignment compiled once while parsing.
        std::function<void(Style&)> m_Apply;

        //! Canonical properties written by this declaration.
        std::vector<std::string> m_Properties;

        //! Whether the declaration overrides ordinary specificity.
        bool m_Important = false;
    };

    //! A selector and its ordered declarations.
    struct Rule
    {
        //! Selector parts ordered from ancestor to subject.
        std::vector<SelectorPart> m_Parts;

        //! Validated declarations in source order.
        std::vector<Declaration> m_Declarations;

        //! Zero for a widget, one for before, and two for after.
        int m_PseudoElement = 0;

        //! Encodes identifier, class, and type specificity.
        unsigned m_Specificity = 0;
    };

    //! Tests the selector against a node and its ancestors.
    static bool Matches(const Rule& rule, const StyleNode& node);

    //! Applies one supported declaration or throws a diagnostic.
    static void Apply(Style& style, const Declaration& declaration);

    //! Converts a validated declaration into typed assignments.
    static void Compile(Declaration& declaration);

    //! Holds compiled named keyframes.
    std::map<std::string, std::vector<Keyframe>, std::less<>> m_Keyframes;

    //! Holds the last successfully parsed rules.
    std::vector<Rule> m_Rules;

    //! Increments only after a successful parse.
    unsigned m_Revision = 0;
};
} // namespace EStudioUI
