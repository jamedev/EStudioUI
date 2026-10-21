#pragma once

#include "Style.h"

#include <string_view>
#include <variant>

namespace EStudioUI
{
//! Individually addressable computed CSS properties.
enum class StyleProperty
{
    Background,
    Color,
    BorderColor,
    SelectionBackground,
    SelectionColor,
    PlaceholderColor,
    CaretColor,
    BorderWidth,
    Radius,
    FontSize,
    Opacity,
    Padding,
    Margin,
    BoxShadow,
    Width,
    Height,
    MinWidth,
    MinHeight,
    MaxWidth,
    MaxHeight,
    Gap,
    FlexGrow,
    Row,
    Displayed,
    Align,
    Justify,
    TextAlign,
    RowHeight,
    Indent,
    HeaderBackground,
    AlternateBackground,
    HoverBackground,
    GridColor,
    TranslateX,
    TranslateY,
    Count
};

//! Typed values accepted by computed-style animation tracks.
using StyleValue = std::variant<float, Color, Insets, Shadow, Length, bool, std::string>;

//! Returns a property's canonical CSS name, rejecting invalid identifiers.
std::string_view GetStylePropertyName(StyleProperty property);

//! Reads a property's typed value from a computed style.
StyleValue GetStyleValue(const Style& style, StyleProperty property);

//! Assigns a typed value, rejecting nonfinite data and clamping bounded magnitudes.
void SetStyleValue(Style& style, StyleProperty property, const StyleValue& value);

//! Returns whether changing a property can retain style and layout caches.
bool IsPaintOnlyProperty(StyleProperty property);
} // namespace EStudioUI
