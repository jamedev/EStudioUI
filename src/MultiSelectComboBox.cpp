#include "PickerButton.h"
#include <EStudioUI/MultiSelectComboBox.h>
#include <EStudioUI/Models.h>
#include <EStudioUI/UiContext.h>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
class ChoiceDelegate final : public ItemDelegate
{
  public:
    explicit ChoiceDelegate(std::weak_ptr<SelectionModel> selection) : m_Selection(std::move(selection))
    {
    }

    std::string GetText(const ModelSnapshot& snapshot, ItemId item, std::size_t column) const override
    {
        const auto selection = m_Selection.lock();
        return std::string(selection && selection->IsSelected(item) ? "[x]  " : "[ ]  ") +
               snapshot.GetText(item, column);
    }

  private:
    std::weak_ptr<SelectionModel> m_Selection;
};

class ToggleList final : public ListWidget
{
  protected:
    void OnPointerDown(const PointerEvent& event) override
    {
        auto modified = event;
        modified.m_Modifiers.m_Control = true;
        modified.m_Modifiers.m_Shift = false;
        ListWidget::OnPointerDown(modified);
    }

    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override
    {
        if (down && key == Key::Space)
        {
            const auto selection = GetSelectionModel();
            const auto current = GetCurrentItemId();
            if (selection && current)
            {
                selection->Select(current, 0, SelectionAction::Toggle);
            }
        }
        else
        {
            ListWidget::OnKey(key, down, repeat, modifiers);
        }
    }
};
} // namespace

MultiSelectComboBox::MultiSelectComboBox() : Widget("multiselectcombobox")
{
    SetLayoutDirection(Axis::Horizontal);
    SetGap(0);
    m_Button = &Add<ToolButton>("Choose…");
    m_Button->SetWidth(SizeRule::Fill());
    m_Button->SetHeight(SizeRule::Fill());
    auto& arrow = Add<Detail::PickerButton>(Detail::PickerButton::Symbol::Down);
    arrow.SetAccessibleName("Show choices");
    arrow.SetHeight(SizeRule::Fill());
    const WidgetRef self(this);
    arrow.Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<MultiSelectComboBox*>(self.Get()))
            {
                live->ShowPopup();
            }
        });
    m_Button->Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<MultiSelectComboBox*>(self.Get()))
            {
                live->ShowPopup();
            }
        });
}

void MultiSelectComboBox::SetItems(std::vector<std::string> items)
{
    VerifyStructuralMutation();
    std::size_t bytes = 0;
    for (const auto& item : items)
    {
        bytes += item.size();
    }
    if (items.size() > 4096 || bytes > 1048576)
    {
        throw std::length_error("Multi-select captions are limited to 4096 rows and 1 MiB.");
    }
    if (!GetContext())
    {
        m_Pending = std::move(items);
        return;
    }

    struct Choice
    {
        std::string m_Text;
    };

    std::vector<Choice> rows;
    for (auto& item : items)
    {
        rows.push_back({std::move(item)});
    }
    SetModel(MakeTableModel(GetContext()->States(), std::move(rows), Column("Choice", &Choice::m_Text)));
}

void MultiSelectComboBox::EnsureModel()
{
    if (!GetContext())
    {
        throw std::logic_error("Attach the multiple-choice selector first.");
    }
    if (!m_Model)
    {
        SetItems(std::move(m_Pending));
    }
}

void MultiSelectComboBox::SetModel(std::shared_ptr<ItemModel> model, std::size_t column)
{
    VerifyStructuralMutation();
    if (!GetContext() || !model || &model->GetStore() != &GetContext()->States())
    {
        throw std::invalid_argument("Multi-select model must belong to the control's state domain.");
    }
    const auto validate = [column](const ModelSnapshot& snapshot)
    {
        if (column >= snapshot.GetColumns().size() || snapshot.GetRowCount() > 1000000)
        {
            throw std::invalid_argument("Multi-select requires a valid column and at most 1000000 top-level rows.");
        }
    };
    validate(model->GetSnapshot());
    auto selection = SelectionModel::Create(model);
    selection->SetMode(SelectionMode::Multiple);
    const WidgetRef self(this);
    auto guard = selection->AddValidator(
        [weak = std::weak_ptr<SelectionModel>(selection)]
        {
            if (const auto live = weak.lock(); live && live->GetSnapshot()->m_Items.size() > 128)
            {
                throw std::length_error("Multi-select is limited to 128 selected items.");
            }
        });
    auto modelGuard = model->AddValidator(validate);
    HidePopup();
    m_Connection.reset();
    m_Guard.reset();
    m_ModelConnection.reset();
    m_ModelGuard.reset();
    m_Model = std::move(model);
    m_Selection = std::move(selection);
    m_Column = column;
    m_Guard.emplace(std::move(guard));
    m_ModelGuard.emplace(std::move(modelGuard));
    m_Connection.emplace(m_Selection->Subscribe(
        [self](const StateChange<SelectionState>&)
        {
            if (auto* live = dynamic_cast<MultiSelectComboBox*>(self.Get()))
            {
                live->Sync();
            }
        }));
    m_ModelConnection.emplace(m_Model->Subscribe(
        [self](const ModelChange&)
        {
            if (auto* live = dynamic_cast<MultiSelectComboBox*>(self.Get()))
            {
                live->Sync();
            }
        }));
    Sync();
}

std::shared_ptr<ItemModel> MultiSelectComboBox::GetModel()
{
    VerifyAccess();
    if (!m_Model)
    {
        VerifyStructuralMutation();
        EnsureModel();
    }
    return m_Model;
}

std::shared_ptr<SelectionModel> MultiSelectComboBox::GetSelectionModel()
{
    VerifyAccess();
    if (!m_Model)
    {
        VerifyStructuralMutation();
        EnsureModel();
    }
    return m_Selection;
}

void MultiSelectComboBox::SetSelectedItems(std::vector<ItemId> items)
{
    VerifyStructuralMutation();
    EnsureModel();
    if (items.size() > 128)
    {
        throw std::length_error("Select at most 128 items.");
    }
    m_Selection->SetItems(std::move(items));
}

void MultiSelectComboBox::Sync()
{
    const auto selected = m_Selection->GetSnapshot();
    const auto snapshot = m_Model->GetSnapshot();
    std::string text;
    std::vector<ItemId> ids;
    for (const auto id : selected->m_Items)
    {
        ids.push_back(id);
        if (ids.size() <= 3 && snapshot.Contains(id))
        {
            if (!text.empty())
            {
                text += ", ";
            }
            auto caption = snapshot.GetText(id, m_Column);
            if (caption.size() > 256)
            {
                auto end = std::size_t{256};
                while (end && (static_cast<unsigned char>(caption[end]) & 0xc0) == 0x80)
                {
                    --end;
                }
                caption.resize(end);
                caption += "…";
            }
            text += caption;
        }
    }
    if (ids.size() > 3)
    {
        text += " +" + std::to_string(ids.size() - 3);
    }
    m_Button->SetText(text.empty() ? "Choose…" : text);
    m_SelectionChanged.Emit(std::move(ids));
}

void MultiSelectComboBox::ShowPopup()
{
    VerifyStructuralMutation();
    EnsureModel();
    Popup* popup = dynamic_cast<Popup*>(m_Popup.Get());
    if (popup && popup->GetContext() != GetContext())
    {
        popup = nullptr;
    }
    if (!popup)
    {
        auto& created = Popup::Create(*this);
        created.AddClass("multi-select-popup");
        auto& list = created.Add<ToggleList>();
        list.SetWidth(SizeRule::Fill());
        list.SetHeight(SizeRule::Fill());
        popup = &created;
        m_Popup = WidgetRef(popup);
    }
    auto& list = static_cast<ListWidget&>(*popup->GetChildren().front());
    list.SetModel(m_Model, m_Column);
    list.SetSelectionModel(m_Selection);
    list.SetItemDelegate(std::make_shared<const ChoiceDelegate>(m_Selection));
    popup->SetWidth(SizeRule::Fixed(std::max(240.0f, GetBounds().m_Width)));
    popup->SetHeight(SizeRule::Fixed(240));
    popup->Open(*this);
}

void MultiSelectComboBox::HidePopup()
{
    VerifyStructuralMutation();
    if (auto* popup = dynamic_cast<Popup*>(m_Popup.Get()); popup && popup->IsOpen())
    {
        popup->Close();
    }
}

Signal<std::vector<ItemId>>& MultiSelectComboBox::SelectionChanged()
{
    VerifyAccess();
    return m_SelectionChanged;
}

bool MultiSelectComboBox::OnKeyPreview(Key key, bool down, bool, Modifiers modifiers)
{
    if (down && ((modifiers.m_Alt && key == Key::Down) || key == Key::Space))
    {
        ShowPopup();
        return true;
    }
    return false;
}
} // namespace EStudioUI
