#include <EStudioUI/PropertyModel.h>

#include <cmath>
#include <stdexcept>

namespace EStudioUI
{
PropertyModel::PropertyModel(StateStore& store, std::vector<PropertyDefinition> definitions)
{
    if (definitions.size() > 16384)
    {
        throw std::length_error("Property models support at most 16,384 properties.");
    }
    std::vector<PropertyValue> values;
    for (std::size_t index = 0; index < definitions.size(); ++index)
    {
        const auto& property = definitions[index];
        if (!m_Index.emplace(property.m_Id, index).second)
        {
            throw std::invalid_argument("Duplicate property identity: " + property.m_Id);
        }
        ValidatePropertyDefinition(property);
        values.push_back(property.m_Default);
    }
    m_Definitions = std::make_shared<const std::vector<PropertyDefinition>>(std::move(definitions));
    m_Values = std::make_unique<State<std::vector<PropertyValue>>>(
        store, std::move(values),
        [schema = m_Definitions](const std::vector<PropertyValue>& candidate)
        {
            if (candidate.size() != schema->size())
            {
                throw std::invalid_argument("A property commit must preserve its immutable schema.");
            }
            for (std::size_t index = 0; index < candidate.size(); ++index)
            {
                ValidatePropertyValue((*schema)[index], candidate[index]);
            }
        });
}

const std::vector<PropertyDefinition>& PropertyModel::GetDefinitions() const
{
    (void)m_Values->Get();
    return *m_Definitions;
}

std::size_t PropertyModel::Find(std::string_view id) const
{
    (void)m_Values->Get();
    const auto found = m_Index.find(std::string(id));
    if (found == m_Index.end())
    {
        throw std::out_of_range("Unknown property: " + std::string(id));
    }
    return found->second;
}

PropertyValue PropertyModel::GetValue(std::string_view id) const
{
    return m_Values->Get().at(Find(id));
}

void PropertyModel::SetValue(std::string_view id, PropertyValue value)
{
    auto candidate = m_Values->Get();
    candidate.at(Find(id)) = std::move(value);
    m_Values->Set(std::move(candidate));
}

State<std::vector<PropertyValue>>& PropertyModel::Values()
{
    (void)m_Values->Get();
    return *m_Values;
}
} // namespace EStudioUI
