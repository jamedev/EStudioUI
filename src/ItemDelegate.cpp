#include <EStudioUI/ItemDelegate.h>

namespace EStudioUI
{
ItemDelegate::~ItemDelegate() = default;

std::string ItemDelegate::GetText(const ModelSnapshot& snapshot, ItemId item, std::size_t column) const
{
    return snapshot.GetText(item, column);
}

Style ItemDelegate::GetStyle(const Style& css, const ModelSnapshot& snapshot, ItemId item, std::size_t column,
                             bool selected) const
{
    auto result = css;
    if (!selected)
    {
        if (const auto value = snapshot.GetValue(item, column, ModelRole::Foreground);
            std::holds_alternative<Color>(value))
            result.m_Color = std::get<Color>(value);
        if (const auto value = snapshot.GetValue(item, column, ModelRole::Background);
            std::holds_alternative<Color>(value))
        {
            result.m_Background = std::get<Color>(value);
            result.m_BackgroundImage = {};
        }
    }
    if (const auto value = snapshot.GetValue(item, column, ModelRole::Enabled);
        std::holds_alternative<bool>(value) && !std::get<bool>(value))
        result.m_Opacity *= 0.5f;
    return result;
}

bool ItemDelegate::Paint(Renderer&, FontFace&, const ModelSnapshot&, ItemId, std::size_t, Rect, const Style&, Rect,
                         float) const
{
    return false;
}

std::string ItemDelegate::GetEditorText(const ModelSnapshot& snapshot, ItemId item, std::size_t column) const
{
    return FormatModelValue(snapshot.GetValue(item, column));
}

std::unique_ptr<CellEditor> ItemDelegate::CreateEditor(const ModelSnapshot& snapshot, ItemId item,
                                                       std::size_t column) const
{
    const auto& schema = snapshot.GetColumns().at(column);
    return schema.m_EditorType || schema.m_Nullable || schema.m_ValueType == ModelValueType::Blob ||
                   !schema.m_Choices.empty()
               ? CreateTypedCellEditor(schema, snapshot.GetValue(item, column))
               : nullptr;
}

ModelValue ItemDelegate::ParseEdit(const ModelSnapshot& snapshot, ItemId, std::size_t column,
                                   std::string_view text) const
{
    return ParseModelValue(snapshot.GetColumns().at(column), text);
}
} // namespace EStudioUI
