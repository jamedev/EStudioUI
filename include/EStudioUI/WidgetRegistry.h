#pragma once

#include "PropertyValue.h"
#include "Widget.h"

#include <map>

namespace EStudioUI
{
//! One editable property shared by runtime construction and direct C++ generation.
struct UiPropertyDescriptor
{
    //! Immutable editor and validation metadata.
    PropertyDefinition m_Definition;

    //! Applies a validated canonical value to an actual widget.
    std::function<void(Widget&, const PropertyValue&)> m_Apply;

    //! Emits one direct C++ statement for the supplied variable and value.
    std::function<std::string(std::string_view, const PropertyValue&)> m_EmitCpp;
};

//! One explicit embeddable-widget adapter; no placeholder substitution is performed.
struct UiWidgetDescriptor
{
    //! Stable serialized type name.
    std::string m_Type;

    //! Palette category.
    std::string m_Category;

    //! Trusted C++ class name and public include path.
    std::string m_CppType, m_Include;

    //! Constructs an actual detached widget without native window or event-loop ownership.
    std::function<std::unique_ptr<Widget>()> m_Create;

    //! Resolves an ordinary content slot; empty means a leaf.
    std::function<Widget&(Widget&)> m_Content;

    //! Equivalent direct C++ slot expression, using $ for the widget variable.
    std::string m_CppContent;

    //! Complete immutable public property adapters.
    std::vector<UiPropertyDescriptor> m_Properties;
};

//! Explicit, extensible construction and property metadata for UI authoring.
class WidgetRegistry
{
  public:
    //! Creates the built-in embeddable control catalog.
    WidgetRegistry();

    //! Adds one trusted adapter, rejecting duplicate types and incomplete metadata.
    void Register(UiWidgetDescriptor descriptor);

    //! Returns immutable descriptors in stable type order.
    const std::map<std::string, UiWidgetDescriptor>& GetTypes() const;

    //! Resolves a type or reports a precise unsupported-widget error.
    const UiWidgetDescriptor& Find(std::string_view type) const;

    //! Resolves one complete editable property adapter.
    const UiPropertyDescriptor& FindProperty(std::string_view type, std::string_view property) const;

  private:
    //! Owns immutable adapters after registration.
    std::map<std::string, UiWidgetDescriptor> m_Types;
};

//! Emits an injection-safe ordinary C++ string literal from exact UTF-8 bytes.
std::string UiCppString(std::string_view text);

//! Emits a direct typed C++ value without JSON or runtime parsing.
std::string UiCppValue(const PropertyValue& value);
} // namespace EStudioUI
