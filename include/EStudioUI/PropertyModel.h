#pragma once

#include "PropertyValue.h"
#include "State.h"

#include <unordered_map>

namespace EStudioUI
{
//! Canonical validated property values with an immutable schema and coherent State transactions.
class PropertyModel
{
  public:
    //! Creates at most 16,384 distinct typed properties in the supplied owner-side store.
    PropertyModel(StateStore& store, std::vector<PropertyDefinition> definitions);

    //! Returns immutable metadata after owner verification.
    const std::vector<PropertyDefinition>& GetDefinitions() const;

    //! Finds a stable property identity, throwing if absent.
    std::size_t Find(std::string_view id) const;

    //! Reads a canonical value by stable identity.
    PropertyValue GetValue(std::string_view id) const;

    //! Changes one value through final-candidate validation and coherent notification.
    void SetValue(std::string_view id, PropertyValue value);

    //! Exposes shared State for validation, transactions, bindings and owned snapshots.
    State<std::vector<PropertyValue>>& Values();

  private:
    //! Shares immutable metadata with validators without retaining this facade.
    std::shared_ptr<const std::vector<PropertyDefinition>> m_Definitions;

    //! Maps stable IDs without relying on ownership indices.
    std::unordered_map<std::string, std::size_t> m_Index;

    //! Owns the validated canonical sheet.
    std::unique_ptr<State<std::vector<PropertyValue>>> m_Values;
};
} // namespace EStudioUI
