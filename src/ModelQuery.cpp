#include <EStudioUI/ModelQuery.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace EStudioUI
{
namespace
{
void ValidateFilter(const ModelFilter& filter, std::size_t columns, unsigned depth, std::size_t& nodes)
{
    if (++nodes > 128 || depth > 16)
    {
        throw std::length_error("A filter accepts at most 128 expressions and 16 nested groups.");
    }
    if (filter.m_Operator < ModelFilterOperator::Equal || filter.m_Operator > ModelFilterOperator::IsNotNull)
    {
        throw std::invalid_argument("Unknown filter operator.");
    }
    if (filter.m_Column && (*filter.m_Column >= columns || !filter.m_Children.empty()))
    {
        throw std::invalid_argument("A filter leaf requires one existing column and no children.");
    }
    if (const auto* text = std::get_if<std::string>(&filter.m_Value);
        text && (text->size() > 65536 || text->find('\0') != std::string::npos))
    {
        throw std::length_error("A text filter accepts at most 65536 bytes without embedded zeroes.");
    }
    if (const auto* bytes = std::get_if<ModelBlob>(&filter.m_Value); bytes && bytes->size() > 65536)
    {
        throw std::length_error("A binary filter accepts at most 65536 bytes.");
    }
    ModelColumn any;
    any.m_ValueType = ModelValueType::Any;
    ValidateModelValue(any, filter.m_Value);
    if (filter.m_Column &&
        (filter.m_Operator == ModelFilterOperator::Contains || filter.m_Operator == ModelFilterOperator::StartsWith) &&
        !std::holds_alternative<std::string>(filter.m_Value))
    {
        throw std::invalid_argument("Text search requires an owned text pattern.");
    }
    if (std::holds_alternative<Color>(filter.m_Value))
    {
        throw std::invalid_argument("Portable data-source filters do not accept colors.");
    }
    for (const auto& child : filter.m_Children)
    {
        ValidateFilter(child, columns, depth + 1, nodes);
    }
}

std::string FoldAscii(std::string value)
{
    for (auto& c : value)
    {
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return value;
}

bool Matches(const ModelSnapshot& snapshot, ItemId item, const ModelFilter& filter)
{
    if (!filter.m_Column)
    {
        for (const auto& child : filter.m_Children)
        {
            const bool accepted = Matches(snapshot, item, child);
            if (accepted && filter.m_Any)
            {
                return true;
            }
            if (!accepted && !filter.m_Any)
            {
                return false;
            }
        }
        return !filter.m_Any;
    }
    auto value = snapshot.GetValue(item, *filter.m_Column);
    const bool null = std::holds_alternative<std::monostate>(value);
    const bool expectedNull = std::holds_alternative<std::monostate>(filter.m_Value);
    if (filter.m_Operator == ModelFilterOperator::IsNull)
    {
        return null;
    }
    if (filter.m_Operator == ModelFilterOperator::IsNotNull)
    {
        return !null;
    }
    if (expectedNull)
    {
        return filter.m_Operator == ModelFilterOperator::Equal
                   ? null
                   : filter.m_Operator == ModelFilterOperator::NotEqual && !null;
    }
    if (null)
    {
        return false;
    }
    auto expected = filter.m_Value;
    if (!filter.m_CaseSensitive)
    {
        if (auto* text = std::get_if<std::string>(&value))
        {
            *text = FoldAscii(*text);
        }
        if (auto* text = std::get_if<std::string>(&expected))
        {
            *text = FoldAscii(*text);
        }
    }
    const int order = CompareModelValues(value, expected);
    switch (filter.m_Operator)
    {
    case ModelFilterOperator::Equal:
        return order == 0;
    case ModelFilterOperator::NotEqual:
        return order != 0;
    case ModelFilterOperator::Less:
        return order < 0;
    case ModelFilterOperator::LessEqual:
        return order <= 0;
    case ModelFilterOperator::Greater:
        return order > 0;
    case ModelFilterOperator::GreaterEqual:
        return order >= 0;
    case ModelFilterOperator::Contains:
    case ModelFilterOperator::StartsWith:
        if (const auto* text = std::get_if<std::string>(&value))
        {
            if (const auto* pattern = std::get_if<std::string>(&expected))
            {
                return filter.m_Operator == ModelFilterOperator::Contains ? text->find(*pattern) != std::string::npos
                                                                          : text->starts_with(*pattern);
            }
        }
        return false;
    default:
        return false;
    }
}
} // namespace

void ModelQuery::Validate(std::size_t columns) const
{
    if (m_DataSet.empty() || m_DataSet.size() > 4096 || m_DataSet.find('\0') != std::string::npos)
    {
        throw std::invalid_argument("A query requires a bounded source dataset identifier.");
    }
    if (!m_Limit || m_Limit > 2048 ||
        m_Offset > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - m_Limit)
    {
        throw std::out_of_range("A result window requires 1..2048 rows and a finite signed-64-bit offset.");
    }
    if (m_Sort.size() > 16)
    {
        throw std::length_error("A query accepts at most 16 sort keys.");
    }
    std::unordered_set<std::size_t> sorted;
    for (const auto& sort : m_Sort)
    {
        if (sort.m_Column >= columns || !sorted.insert(sort.m_Column).second)
        {
            throw std::invalid_argument("Sort keys require unique existing schema columns.");
        }
    }
    std::size_t nodes = 0;
    ValidateFilter(m_Filter, columns, 0, nodes);
}

std::function<bool(const ModelSnapshot&, ItemId)> CompileModelFilter(ModelFilter filter, std::size_t columns)
{
    std::size_t nodes = 0;
    ValidateFilter(filter, columns, 0, nodes);
    return [filter = std::move(filter)](const ModelSnapshot& snapshot, ItemId item)
    { return Matches(snapshot, item, filter); };
}

bool MatchesModelFilter(const ModelSnapshot& snapshot, ItemId item, const ModelFilter& filter)
{
    std::size_t nodes = 0;
    ValidateFilter(filter, snapshot.GetColumns().size(), 0, nodes);
    return Matches(snapshot, item, filter);
}
} // namespace EStudioUI
