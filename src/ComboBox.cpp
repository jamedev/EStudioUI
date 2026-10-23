#include <EStudioUI/ComboBox.h>
#include <EStudioUI/Models.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>
#include <utf8proc.h>
#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
class DropButton final : public ToolButton
{
  public:
    DropButton() : ToolButton("")
    {
    }

    bool IsFocusable() const override
    {
        VerifyAccess();
        return false;
    }

  protected:
    Point Measure(FontFace&) const override
    {
        return {14, 14};
    }

    void Paint(Renderer& renderer, FontFace&, double, float opacity) const override
    {
        renderer.DrawBox(GetBounds(), GetStyle(), GetClip(), opacity);
        const auto b = GetContentBounds();
        const Point center{b.m_X + b.m_Width * 0.5f, b.m_Y + b.m_Height * 0.5f};
        auto color = GetStyle().m_Color;
        color.m_A *= opacity;
        renderer.DrawLine({center.m_X - 4, center.m_Y - 2}, {center.m_X, center.m_Y + 2}, color, 2, GetClip());
        renderer.DrawLine({center.m_X, center.m_Y + 2}, {center.m_X + 4, center.m_Y - 2}, color, 2, GetClip());
    }
};

bool Enabled(const ModelSnapshot& snapshot, ItemId id, std::size_t column)
{
    const auto value = snapshot.GetValue(id, column, ModelRole::Enabled);
    return !std::holds_alternative<bool>(value) || std::get<bool>(value);
}

struct Choice
{
    std::string m_Text;
};

std::string Fold(std::string_view text)
{
    if (text.empty())
        return {};
    if (text.size() > 1048576)
        throw std::length_error("Combo search text is limited to 1 MiB.");
    utf8proc_uint8_t* output = nullptr;
    const auto size =
        utf8proc_map(reinterpret_cast<const utf8proc_uint8_t*>(text.data()), static_cast<utf8proc_ssize_t>(text.size()),
                     &output, static_cast<utf8proc_option_t>(UTF8PROC_STABLE | UTF8PROC_COMPOSE | UTF8PROC_CASEFOLD));
    const std::unique_ptr<utf8proc_uint8_t, decltype(&std::free)> owner(output, std::free);
    if (size < 0)
        throw std::invalid_argument("Combo captions/search must be valid UTF-8.");
    return std::string(reinterpret_cast<const char*>(output), static_cast<std::size_t>(size));
}
} // namespace

ComboBox::ComboBox() : Widget("combobox")
{
    SetLayoutDirection(Axis::Horizontal);
    SetGap(0);
    SetState(ReadOnly, true);
    SetState(Empty, true);
    m_Editor = &Add<TextBox>();
    m_Editor->AddClass("combo-editor");
    m_Editor->SetWidth(SizeRule::Fill());
    m_Editor->SetHeight(SizeRule::Fill());
    m_Editor->SetReadOnly(true);
    m_Arrow = &Add<DropButton>();
    m_Arrow->AddClass("combo-arrow");
    m_Arrow->SetAccessibleName("Show choices");
    m_Arrow->SetWidth(SizeRule::Fixed(32));
    m_Arrow->SetHeight(SizeRule::Fill());
    const WidgetRef self(this);
    m_Arrow->Clicked().Connect(
        [self]
        {
            if (auto* live = dynamic_cast<ComboBox*>(self.Get()))
            {
                if (live->IsPopupOpen())
                    live->HidePopup();
                else
                {
                    if (live->GetContext())
                        live->GetContext()->SetFocus(&live->GetEditor());
                    if (auto* current = dynamic_cast<ComboBox*>(self.Get()))
                        current->ShowPopup();
                }
            }
        });
    m_Editor->TextChanged().Connect(
        [self](const std::string&)
        {
            if (auto* live = dynamic_cast<ComboBox*>(self.Get()); live && !live->m_Syncing)
                live->Edited();
        });
}

std::vector<std::pair<std::string, ItemId>> ComboBox::BuildIndex(const ModelSnapshot& snapshot, std::size_t column)
{
    if (column >= snapshot.GetColumns().size())
        throw std::out_of_range("Invalid combo model column.");
    if (snapshot.GetRowCount() > 1000000)
        throw std::length_error("Combo prefix indexing is limited to 1,000,000 top-level items.");
    std::vector<std::pair<std::string, ItemId>> index;
    if (snapshot.GetRowCount() > 32 * 1024 * 1024 / sizeof(std::pair<std::string, ItemId>))
        throw std::length_error("Combo index metadata exceeds 32 MiB.");
    index.reserve(snapshot.GetRowCount());
    std::size_t bytes = index.capacity() * sizeof(index.front());
    for (std::size_t row = 0; row < snapshot.GetRowCount(); ++row)
    {
        const auto id = snapshot.GetItem(row);
        auto text = Fold(snapshot.GetText(id, column));
        bytes += text.capacity() + 1;
        if (bytes > 32 * 1024 * 1024)
            throw std::length_error("Combo prefix index exceeds 32 MiB.");
        if (Enabled(snapshot, id, column))
            index.emplace_back(std::move(text), id);
    }
    std::stable_sort(index.begin(), index.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return index;
}

void ComboBox::SetItems(std::vector<std::string> items)
{
    VerifyStructuralMutation();
    if (items.size() > 1000000)
        throw std::length_error("Combo supports at most 1,000,000 top-level items.");
    if (!GetContext())
    {
        for (const auto& text : items)
            (void)Fold(text);
        m_PendingItems = std::move(items);
        m_DetachedIndex = m_PendingItems->empty() ? -1 : 0;
        m_Syncing = true;
        try
        {
            m_Editor->SetText(m_DetachedIndex < 0 ? std::string{} : m_PendingItems->front());
            m_Syncing = false;
        }
        catch (...)
        {
            m_Syncing = false;
            throw;
        }
        return;
    }
    std::vector<Choice> records;
    records.reserve(items.size());
    for (auto& text : items)
        records.push_back({std::move(text)});
    const WidgetRef self(this);
    SetModel(MakeTableModel(GetContext()->States(), std::move(records), Column("Text", &Choice::m_Text)));
    if (self.Get() && GetItemCount())
        SetCurrentIndex(0);
}

void ComboBox::EnsureModel()
{
    if (m_View)
        return;
    if (!GetContext())
        throw std::logic_error("Combo popup/model operations require an attached idle control.");
    auto items = m_PendingItems ? std::move(*m_PendingItems) : std::vector<std::string>{};
    const int selected = m_DetachedIndex;
    m_PendingItems.reset();
    const WidgetRef self(this);
    SetItems(std::move(items));
    if (!self.Get())
        return;
    if (selected >= 0 && selected < GetItemCount())
        SetCurrentIndex(selected);
}

void ComboBox::SetModel(std::shared_ptr<ItemModel> model, std::size_t column)
{
    VerifyStructuralMutation();
    if (!GetContext() || !model)
        throw std::invalid_argument("Combo requires an attached control and a live model.");
    if (m_View && m_View->GetModel() == model && m_Column == column)
        return;
    auto index = BuildIndex(model->GetSnapshot(), column);
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    HidePopup();
    if (!self.Get())
        return;
    const auto changed = [](Widget& widget, const ModelChange&) { static_cast<ComboBox&>(widget).ModelChanged(); };
    ScopedConnection guard(
        model->AddValidator([column](const ModelSnapshot& candidate) { (void)BuildIndex(candidate, column); }));
    auto view = std::make_unique<CollectionView>(*this, model, changed, [](Widget& widget)
                                                 { static_cast<ComboBox&>(widget).SyncSelection(); });
    m_ModelGuard.emplace(std::move(guard));
    m_View = std::move(view);
    m_Column = column;
    m_Index = std::move(index);
    IndexEnabledRows();
    SetState(Empty, GetItemCount() == 0);
    m_PendingItems.reset();
    if (auto* popup = dynamic_cast<ListWidget*>(m_Popup.Get()); popup && popup->GetContext() == GetContext())
        popup->SetModel(model, column);
    SyncSelection();
}

std::shared_ptr<ItemModel> ComboBox::GetModel() const
{
    VerifyAccess();
    return m_View ? m_View->GetModel() : nullptr;
}

std::shared_ptr<SelectionModel> ComboBox::GetSelectionModel() const
{
    VerifyAccess();
    return m_View ? m_View->GetSelectionModel() : nullptr;
}

void ComboBox::SetSelectionModel(std::shared_ptr<SelectionModel> selection)
{
    VerifyStructuralMutation();
    const WidgetRef self(this);
    EnsureModel();
    if (!self.Get())
        return;
    m_View->SetSelectionModel(std::move(selection));
    SyncSelection();
}

int ComboBox::GetItemCount() const
{
    VerifyAccess();
    return m_View           ? static_cast<int>(m_View->GetModel()->GetSnapshot().GetRowCount())
           : m_PendingItems ? static_cast<int>(m_PendingItems->size())
                            : 0;
}

int ComboBox::GetCurrentIndex() const
{
    VerifyAccess();
    if (!m_View)
        return m_DetachedIndex;
    const auto data = m_View->GetModel()->GetSnapshot();
    const auto id = m_View->GetSelectionModel()->GetCurrent();
    if (!id || !data.Contains(id) || data.GetParent(id))
        return -1;
    const auto position = data.GetPosition(id);
    return position ? static_cast<int>(*position) : -1;
}

void ComboBox::SetCurrentIndex(int index)
{
    VerifyStructuralMutation();
    if (index < -1 || index >= GetItemCount())
        throw std::out_of_range("Invalid combo index.");
    if (!m_View && !GetContext())
    {
        m_DetachedIndex = index;
        m_Syncing = true;
        try
        {
            m_Editor->SetText(index < 0 ? std::string{} : (*m_PendingItems)[static_cast<std::size_t>(index)]);
            m_Syncing = false;
        }
        catch (...)
        {
            m_Syncing = false;
            throw;
        }
        return;
    }
    const WidgetRef self(this);
    EnsureModel();
    if (!self.Get())
        return;
    const auto selection = m_View->GetSelectionModel();
    if (index < 0)
        selection->Clear();
    else
        selection->Select(m_View->GetModel()->GetSnapshot().GetItem(static_cast<std::size_t>(index)), m_Column);
}

const std::string& ComboBox::GetCurrentText() const
{
    VerifyAccess();
    return m_Editor->GetText();
}

void ComboBox::SetEditable(bool editable)
{
    VerifyStructuralMutation();
    if (m_Editable == editable)
        return;
    m_Editable = editable;
    m_Editor->SetReadOnly(!editable);
    SetState(ReadOnly, !editable);
    NotifySemanticsChanged();
}

bool ComboBox::IsEditable() const
{
    VerifyAccess();
    return m_Editable;
}

TextBox& ComboBox::GetEditor() const
{
    VerifyAccess();
    return *m_Editor;
}

void ComboBox::SyncSelection()
{
    if (!m_View)
        return;
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    const auto id = m_View->GetSelectionModel()->GetCurrent();
    const auto canonical = m_View->GetModel()->GetRootModel()->GetSnapshot();
    const int index = GetCurrentIndex();
    const auto caption = id && canonical.Contains(id) ? canonical.GetText(id, m_Column) : std::string{};
    const bool changed = id != m_LastItem || index != m_LastIndex;
    m_LastItem = id;
    m_LastIndex = index;
    if (!m_Syncing)
    {
        m_Syncing = true;
        try
        {
            m_Editor->SetText(caption);
            if (!self.Get())
                return;
            m_Syncing = false;
        }
        catch (...)
        {
            if (self.Get())
                m_Syncing = false;
            throw;
        }
    }
    NotifySemanticsChanged();
    if (changed && m_View->GetSelectionModel()->GetCurrent() == id && GetCurrentIndex() == index)
        m_CurrentChanged.Emit(index);
}

void ComboBox::IndexEnabledRows()
{
    const auto snapshot = m_View->GetModel()->GetSnapshot();
    std::vector<int> rows;
    rows.reserve(m_Index.size());
    for (const auto& entry : m_Index)
        if (const auto row = snapshot.GetPosition(entry.second))
            rows.push_back(static_cast<int>(*row));
    std::sort(rows.begin(), rows.end());
    m_EnabledRows = std::move(rows);
}

void ComboBox::ModelChanged()
{
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    auto index = BuildIndex(m_View->GetModel()->GetSnapshot(), m_Column);
    m_Index = std::move(index);
    IndexEnabledRows();
    SetState(Empty, GetItemCount() == 0);
    if (self.Get())
        SyncSelection();
}

void ComboBox::Edited()
{
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    const std::string text = m_Editor->GetText();
    if (m_Editable && m_View && m_View->GetSelectionModel()->GetCurrent())
    {
        m_Syncing = true;
        const auto selection = m_View->GetSelectionModel();
        try
        {
            selection->Clear();
            if (!self.Get())
                return;
            m_Syncing = false;
        }
        catch (...)
        {
            if (self.Get())
                m_Syncing = false;
            throw;
        }
        // Clearing a typed selection must retain the user's committed field text.
        m_Syncing = true;
        m_Editor->SetText(text);
        if (!self.Get())
            return;
        m_Syncing = false;
    }
    if (IsPopupOpen())
    {
        const auto match = FindCompletion(text);
        if (!self.Get())
            return;
        if (auto* popup = dynamic_cast<ListWidget*>(m_Popup.Get()); popup && match)
            popup->SetCurrentItemId(*match);
    }
    NotifySemanticsChanged();
    m_EditTextChanged.Emit(text);
}

void ComboBox::SetCompletionProvider(std::function<std::optional<ItemId>(std::string_view)> provider)
{
    VerifyStructuralMutation();
    m_Completion = std::move(provider);
}

std::optional<ItemId> ComboBox::FindCompletion(std::string_view prefix) const
{
    VerifyAccess();
    const auto provider = m_Completion;
    const auto model = GetModel();
    if (provider)
    {
        const auto id = provider(prefix);
        if (id && model && model->GetSnapshot().Contains(*id) && !model->GetSnapshot().GetParent(*id) &&
            Enabled(model->GetSnapshot(), *id, m_Column))
            return id;
        return {};
    }
    const auto folded = Fold(prefix);
    const auto found =
        std::lower_bound(m_Index.begin(), m_Index.end(), folded,
                         [](const auto& entry, const std::string& value) { return entry.first < value; });
    return found != m_Index.end() && found->first.starts_with(folded) ? std::optional(found->second) : std::nullopt;
}

ListWidget* ComboBox::EnsurePopup()
{
    const WidgetRef reference(this);
    EnsureModel();
    if (!reference.Get())
        return nullptr;
    if (auto* popup = dynamic_cast<ListWidget*>(m_Popup.Get()); popup && popup->GetContext() == GetContext())
        return popup;
    auto owned = std::make_unique<ListWidget>();
    owned->AddClass("combo-popup");
    auto& list = static_cast<ListWidget&>(GetContext()->AddOwnedPopup(std::move(owned), *this));
    m_Popup = WidgetRef(&list);
    list.SetModel(m_View->GetModel(), m_Column);
    const WidgetRef self(this);
    list.ItemClicked().Connect(
        [self](int index)
        {
            if (auto* live = dynamic_cast<ComboBox*>(self.Get()))
                live->Accept(index);
        });
    list.Activated().Connect(
        [self](int index)
        {
            if (auto* live = dynamic_cast<ComboBox*>(self.Get()))
                live->Accept(index);
        });
    return &list;
}

void ComboBox::ShowPopup()
{
    VerifyStructuralMutation();
    if (!IsEnabled() || !IsVisible() || IsPopupOpen())
        return;
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    auto* popupPointer = EnsurePopup();
    if (!self.Get() || !popupPointer)
        return;
    auto& popup = *popupPointer;
    if (!GetItemCount())
        return;
    popup.SetWidth(SizeRule::Fixed(std::max(120.0f, GetBounds().m_Width)));
    popup.SetCurrentIndex(GetCurrentIndex());
    const auto bounds = GetBounds();
    const auto anchor = GetContext()->ToWindowPoint(*this, {bounds.m_X, bounds.m_Y + bounds.m_Height});
    const auto generation = ++m_Presentation;
    const auto session = GetContext()->ShowPopup(popup, anchor);
    if (!self.Get())
        return;
    SetState(Expanded, session.IsPending());
    NotifySemanticsChanged();
    session.OnFinished(
        [self, generation](PopupResult)
        {
            if (auto* live = dynamic_cast<ComboBox*>(self.Get()); live && live->m_Presentation == generation)
            {
                live->SetState(Expanded, false);
                live->NotifySemanticsChanged();
            }
        });
}

void ComboBox::HidePopup()
{
    VerifyStructuralMutation();
    if (auto* popup = m_Popup.Get();
        popup && GetContext() && popup->GetContext() == GetContext() && GetContext()->IsPopupOpen(*popup))
        GetContext()->ClosePopup(*popup);
}

bool ComboBox::IsPopupOpen() const
{
    VerifyAccess();
    auto* popup = m_Popup.Get();
    return popup && GetContext() && popup->GetContext() == GetContext() && GetContext()->IsPopupOpen(*popup);
}

void ComboBox::Accept(int index)
{
    const auto lease = RetainOwner();
    const WidgetRef self(this);
    const auto popup = m_Popup;
    SetCurrentIndex(index);
    if (!self.Get())
        return;
    if (auto* live = popup.Get(); live && GetContext()->IsPopupOpen(*live))
        GetContext()->ClosePopup(*live, {PopupCloseReason::Accepted, index});
    if (self.Get())
        m_Activated.Emit(index);
}

bool ComboBox::OnInputPreview(const InputEvent& event)
{
    if (const auto* pointer = std::get_if<PointerEvent>(&event);
        pointer && !m_Editable && pointer->m_Type == PointerEventType::Down &&
        pointer->m_Button == PointerButton::Left && pointer->m_InsideClip &&
        m_Editor->GetBounds().Contains(pointer->m_Position))
    {
        const WidgetRef self(this);
        if (GetContext())
            GetContext()->SetFocus(m_Editor);
        if (self.Get())
            ShowPopup();
        return true;
    }

    if (const auto* key = std::get_if<KeyboardEvent>(&event);
        key && key->m_Down && !key->m_Modifiers.m_Control && !key->m_Modifiers.m_AltGraph)
    {
        if (key->m_Key == Key::F4 || (key->m_Key == Key::Down && key->m_Modifiers.m_Alt) ||
            (!m_Editable && key->m_Key == Key::Space))
        {
            if (IsPopupOpen())
                HidePopup();
            else
                ShowPopup();
            return true;
        }
        if (!m_Editable && !IsPopupOpen() &&
            (key->m_Key == Key::Down || key->m_Key == Key::Up || key->m_Key == Key::Home || key->m_Key == Key::End))
        {
            const WidgetRef owner(this);
            EnsureModel();
            if (!owner.Get())
                return true;
            if (m_EnabledRows.empty())
                return true;
            const int current = GetCurrentIndex();
            auto found = std::lower_bound(m_EnabledRows.begin(), m_EnabledRows.end(), current);
            int index = current;
            if (key->m_Key == Key::Home)
                index = m_EnabledRows.front();
            else if (key->m_Key == Key::End)
                index = m_EnabledRows.back();
            else if (key->m_Key == Key::Up)
                index = found == m_EnabledRows.begin() ? m_EnabledRows.front() : *std::prev(found);
            else
            {
                found = std::upper_bound(m_EnabledRows.begin(), m_EnabledRows.end(), current);
                index = found == m_EnabledRows.end() ? m_EnabledRows.back() : *found;
            }
            SetCurrentIndex(index);
            return true;
        }
    }
    if (const auto* text = std::get_if<TextInputEvent>(&event); text && !m_Editable && !IsPopupOpen())
    {
        const WidgetRef owner(this);
        EnsureModel();
        if (!owner.Get())
            return true;
        const auto now = GetContext()->GetTime();
        if (now > m_SearchUntil)
            m_Search.clear();
        if (m_Search.size() + text->m_Text.size() > 4096)
            m_Search.clear();
        m_Search += text->m_Text;
        m_SearchUntil = now + 1;
        const WidgetRef self(this);
        const auto match = FindCompletion(m_Search);
        if (self.Get() && match)
            m_View->GetSelectionModel()->Select(*match, m_Column);
        return true;
    }
    return false;
}

SemanticInfo ComboBox::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Role = SemanticRole::ComboBox;
    info.m_Value = m_Editor->GetText();
    info.m_ReadOnly = !m_Editable;
    info.m_Expandable = true;
    info.m_Expanded = IsPopupOpen();
    info.m_Actions = SemanticAction::Expand | SemanticAction::Collapse;
    if (m_Editable)
        info.m_Actions = info.m_Actions | SemanticAction::SetText;
    return info;
}

SemanticStatus ComboBox::OnSemanticAction(const SemanticCommand& command)
{
    if (command.m_Action == SemanticAction::Expand)
    {
        ShowPopup();
        return IsPopupOpen() ? SemanticStatus::Succeeded : SemanticStatus::Unavailable;
    }
    if (command.m_Action == SemanticAction::Collapse)
    {
        HidePopup();
        return SemanticStatus::Succeeded;
    }
    if (command.m_Action == SemanticAction::SetText && m_Editable)
    {
        if (!m_Editor->CanSetText(command.m_Text))
            return SemanticStatus::InvalidValue;
        m_Editor->SetText(command.m_Text);
        return SemanticStatus::Succeeded;
    }
    return SemanticStatus::Unsupported;
}

Signal<int>& ComboBox::CurrentChanged()
{
    VerifyAccess();
    return m_CurrentChanged;
}

Signal<int>& ComboBox::Activated()
{
    VerifyAccess();
    return m_Activated;
}

Signal<const std::string&>& ComboBox::EditTextChanged()
{
    VerifyAccess();
    return m_EditTextChanged;
}
} // namespace EStudioUI
