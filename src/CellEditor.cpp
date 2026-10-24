#include <EStudioUI/CellEditor.h>
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/ColorEditor.h>
#include <EStudioUI/ComboBox.h>
#include <EStudioUI/NumericInput.h>
#include <EStudioUI/TextBox.h>
#include <EStudioUI/ToolButton.h>
#include <EStudioUI/UiContext.h>
#include <algorithm>

namespace EStudioUI
{
CellEditor::CellEditor() : Widget("cell-editor")
{
    AddClass("cell-editor");
    SetLayoutDirection(Axis::Horizontal);
    SetStyleProperty(StyleProperty::Padding, Insets{});
    SetStyleProperty(StyleProperty::Gap, 2.0f);
}

CellEditor::~CellEditor() = default;

Widget& CellEditor::FocusTarget() const
{
    return const_cast<CellEditor&>(*this);
}

Signal<>& CellEditor::CommitRequested()
{
    VerifyAccess();
    return m_CommitRequested;
}

Signal<>& CellEditor::CancelRequested()
{
    VerifyAccess();
    return m_CancelRequested;
}

namespace
{
class TypedEditor final : public CellEditor
{
  public:
    ModelColumn m_Column;
    CheckBox* m_Null = nullptr;
    CheckBox* m_Boolean = nullptr;
    ComboBox* m_Choice = nullptr;
    NumericInput* m_Number = nullptr;
    TextBox* m_Text = nullptr;
    ColorEditor* m_Color = nullptr;
    Widget* m_Value = nullptr;
    ModelValueType m_Type;
    bool m_IntegerBoolean = false;
    std::string m_CanonicalNumberText;

    TypedEditor(ModelColumn column, const ModelValue& value) : m_Column(std::move(column))
    {
        m_Type = m_Column.m_EditorType.value_or(m_Column.m_ValueType);
        if (m_Column.m_ValueType == ModelValueType::Any && value.index())
        {
            m_Type = static_cast<ModelValueType>(value.index());
        }
        if (m_Column.m_ValueType == ModelValueType::Any && m_Column.m_EditorType == ModelValueType::Boolean &&
            std::holds_alternative<std::int64_t>(value) &&
            (std::get<std::int64_t>(value) == 0 || std::get<std::int64_t>(value) == 1))
        {
            m_Type = ModelValueType::Boolean;
            m_IntegerBoolean = true;
        }
        if (m_Type == ModelValueType::Any)
        {
            m_Type = ModelValueType::Text;
        }
        if (m_Column.m_Choices.size() > 256)
        {
            throw std::length_error("A cell combo accepts at most 256 choices.");
        }
        const WidgetRef self(this);
        const auto accept = [self]
        {
            if (auto* owner = dynamic_cast<CellEditor*>(self.Get()))
            {
                owner->CommitRequested().Emit();
            }
        };
        if (m_Column.m_Nullable)
        {
            m_Null = &Add<CheckBox>("NULL");
            m_Null->AddClass("cell-null");
            m_Null->SetChecked(!value.index());
        }
        const auto* real = std::get_if<double>(&value);
        const bool useNumericInput = !real || (*real >= -1e300 && *real <= 1e300);
        if (!m_Column.m_Choices.empty())
        {
            m_Choice = &Add<ComboBox>();
            m_Value = m_Choice;
            std::vector<std::string> captions;
            for (const auto& choice : m_Column.m_Choices)
            {
                captions.push_back(FormatModelValue(choice));
            }
            m_Choice->SetItems(std::move(captions));
            const auto selected = std::find(m_Column.m_Choices.begin(), m_Column.m_Choices.end(), value);
            m_Choice->SetCurrentIndex(
                selected == m_Column.m_Choices.end() ? 0 : static_cast<int>(selected - m_Column.m_Choices.begin()));
            m_Choice->Activated().Connect([accept](int) { accept(); });
        }
        else if (m_Type == ModelValueType::Boolean)
        {
            m_Boolean = &Add<CheckBox>();
            m_Value = m_Boolean;
            m_Boolean->SetChecked(m_IntegerBoolean ? std::get<std::int64_t>(value) != 0
                                                   : std::holds_alternative<bool>(value) && std::get<bool>(value));
            m_Boolean->Clicked().Connect(accept);
        }
        else if (m_Type == ModelValueType::Color)
        {
            m_Color = &Add<ColorEditor>();
            m_Value = m_Color;
            if (const auto* color = std::get_if<Color>(&value))
            {
                m_Color->SetColor(*color);
            }
        }
        else if (m_Type == ModelValueType::Real && useNumericInput)
        {
            const double initial = std::holds_alternative<double>(value) ? std::get<double>(value) : 0;
            m_Number = &Add<NumericInput>();
            m_Value = m_Number;
            m_Number->SetRange(-1e300, 1e300);
            m_Number->SetDecimals(8);
            m_Number->SetStep(.1);
            m_Number->SetValue(initial);
            m_CanonicalNumberText = m_Number->GetEditor().GetText();
            m_Number->ValueChanged().Connect(
                [self](double)
                {
                    if (auto* editor = dynamic_cast<TypedEditor*>(self.Get()))
                    {
                        editor->m_CanonicalNumberText = editor->m_Number->GetEditor().GetText();
                    }
                });
        }
        else
        {
            std::string text;
            if (const auto* blob = std::get_if<ModelBlob>(&value))
            {
                if (blob->size() > 32768)
                {
                    throw std::length_error("Use a custom binary editor for values larger than 32 KiB; the inline "
                                            "value was not truncated.");
                }
                text = EncodeModelBlob(*blob);
            }
            else if (value.index())
            {
                text = FormatModelValue(value);
            }
            else if (m_Type == ModelValueType::Integer)
            {
                text = "0";
            }
            else if (m_Type == ModelValueType::Blob)
            {
                text = "0x";
            }
            if (text.size() > 65536)
            {
                throw std::length_error(
                    "Use a document editor for values larger than 64 KiB; the inline value was not truncated.");
            }
            m_Text = &Add<TextBox>(std::move(text));
            m_Value = m_Text;
            m_Text->Submitted().Connect([accept](const std::string&) { accept(); });
        }
        m_Value->AddClass("cell-editor-value");
        m_Value->SetWidth(SizeRule::Fill(1, 20));
        m_Value->SetHeight(SizeRule::Fill());
        if (m_Null)
        {
            m_Value->SetEnabled(!m_Null->IsChecked());
            m_Null->Toggled().Connect(
                [self](bool null)
                {
                    if (auto* editor = dynamic_cast<TypedEditor*>(self.Get()))
                    {
                        editor->m_Value->SetEnabled(!null);
                    }
                });
        }
        auto& apply = Add<ToolButton>("OK");
        apply.AddClass("cell-apply");
        apply.SetAccessibleName("Apply cell value");
        apply.SetWidth(SizeRule::Fixed(32));
        apply.SetStyleProperty(StyleProperty::Padding, Insets{});
        apply.Clicked().Connect(accept);
        auto& cancel = Add<ToolButton>("×");
        cancel.AddClass("cell-cancel");
        cancel.SetAccessibleName("Cancel cell edit");
        cancel.SetWidth(SizeRule::Fixed(24));
        cancel.SetStyleProperty(StyleProperty::Padding, Insets{});
        cancel.Clicked().Connect(
            [self]
            {
                if (auto* editor = dynamic_cast<CellEditor*>(self.Get()))
                {
                    editor->CancelRequested().Emit();
                }
            });
    }

    Widget& FocusTarget() const override
    {
        if (m_Null && m_Null->IsChecked())
        {
            return *m_Null;
        }
        if (m_Number)
        {
            return const_cast<TextBox&>(m_Number->GetEditor());
        }
        if (m_Color)
        {
            return m_Color->Editor();
        }
        return *m_Value;
    }

    ModelValue GetValue() const override
    {
        if (m_Null && m_Null->IsChecked())
        {
            return {};
        }
        ModelValue value;
        if (m_Choice)
        {
            value = m_Column.m_Choices.at(static_cast<std::size_t>(m_Choice->GetCurrentIndex()));
        }
        else if (m_Boolean)
        {
            if (m_IntegerBoolean)
            {
                value = std::int64_t{m_Boolean->IsChecked() ? 1 : 0};
            }
            else
            {
                value = m_Boolean->IsChecked();
            }
        }
        else if (m_Color)
        {
            if (!m_Color->Commit())
            {
                throw std::invalid_argument("Invalid cell color.");
            }
            value = m_Color->GetColor();
        }
        else if (m_Number && m_Number->GetEditor().GetText() == m_CanonicalNumberText)
        {
            value = m_Number->GetValue();
        }
        else
        {
            auto column = m_Column;
            column.m_ValueType = m_Type;
            value = ParseModelValue(column, m_Number ? m_Number->GetEditor().GetText() : m_Text->GetText());
        }
        ValidateModelValue(m_Column, value);
        return value;
    }
};
} // namespace

std::unique_ptr<CellEditor> CreateTypedCellEditor(const ModelColumn& column, const ModelValue& value)
{
    return std::make_unique<TypedEditor>(column, value);
}
} // namespace EStudioUI
