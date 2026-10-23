#include "PropertyNotification.h"
#include <EStudioUI/PropertyGrid.h>
#include <EStudioUI/CheckBox.h>
#include <EStudioUI/ColorEditor.h>
#include <EStudioUI/Renderer.h>
#include <EStudioUI/UiContext.h>
#include <EStudioUI/UnitEditor.h>
#include <EStudioUI/VectorEditor.h>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace EStudioUI
{
PropertyGrid::PropertyGrid() : ScrollWidget("propertygrid")
{
    RegisterStylePart("row", Selected | Alternate | Invalid | ReadOnly,
                      [](const Style& owner, unsigned state)
                      {
                          Style result;
                          result.m_Background = state & Selected    ? owner.m_SelectionBackground
                                                : state & Alternate ? owner.m_AlternateBackground
                                                                    : Color{0, 0, 0, 0};
                          result.m_Color = state & Selected ? owner.m_SelectionColor : owner.m_Color;
                          result.m_FontSize = owner.m_FontSize;
                          result.m_Padding = {2, 8, 2, 8};
                          return result;
                      });
    RegisterStylePart("category", Expanded,
                      [](const Style& owner, unsigned)
                      {
                          Style result;
                          result.m_Background = owner.m_HeaderBackground;
                          result.m_Color = owner.m_Color;
                          result.m_FontSize = owner.m_FontSize;
                          result.m_Padding = {2, 8, 2, 8};
                          return result;
                      });
}

void PropertyGrid::SetModel(std::shared_ptr<PropertyModel> model)
{
    VerifyStructuralMutation();
    if (model)
    {
        (void)model->GetDefinitions();
    }
    const WidgetRef self(this);
    EndEdit();
    if (!self.Get())
    {
        return;
    }
    m_Connections.clear();
    m_Model = std::move(model);
    m_SemanticGeneration = UiDispatcher::AllocateTargetKey();
    m_Selected.clear();
    m_Error.clear();
    if (m_Model)
    {
        m_Connections.emplace_back(m_Model->Values().AddValidator(
            [self](const auto&)
            {
                if (auto* live = dynamic_cast<PropertyGrid*>(self.Get()))
                {
                    live->VerifyStructuralMutation();
                }
            }));
        m_Connections.emplace_back(m_Model->Values().Subscribe(
            [self](const auto&)
            {
                if (auto* live = dynamic_cast<PropertyGrid*>(self.Get()))
                {
                    live->RequestRepaint();
                    live->NotifySemanticsChanged();
                    live->SynchronizeEditor();
                }
            }));
    }
    RebuildRows();
    SetScrollOffset({});
}

std::shared_ptr<PropertyModel> PropertyGrid::GetModel() const
{
    VerifyAccess();
    return m_Model;
}

void PropertyGrid::SelectProperty(std::string_view id, bool edit)
{
    VerifyStructuralMutation();
    if (!m_Model)
    {
        throw std::logic_error("Attach a property model before selecting a property.");
    }
    const auto model = m_Model;
    const auto& definition = model->GetDefinitions().at(model->Find(id));
    const auto ownedId = definition.m_Id;
    const auto category = definition.m_Category;
    const bool changed = m_Selected != ownedId;
    const WidgetRef self(this);
    if (changed)
    {
        EndEdit();
    }
    if (!self.Get() || m_Model != model)
    {
        return;
    }
    m_Selected = ownedId;
    NotifySemanticsChanged();
    m_Collapsed.erase(category);
    RebuildRows();
    const auto bounds = GetPropertyBounds(ownedId);
    const auto viewport = GetViewport();
    const auto offset = GetScrollOffset();
    EnsureVisible({0, bounds.m_Y - viewport.m_Y + offset.m_Y, viewport.m_Width, bounds.m_Height});
    if (edit)
    {
        BeginEdit();
    }
    if (self.Get() && changed && m_Model == model && m_Selected == ownedId)
    {
        const auto snapshot = m_Selected;
        m_SelectionChanged.Emit(snapshot);
    }
}

const std::string& PropertyGrid::GetSelectedProperty() const
{
    VerifyAccess();
    return m_Selected;
}

void PropertyGrid::SetCategoryExpanded(std::string category, bool expanded)
{
    VerifyStructuralMutation();
    const WidgetRef self(this);
    if (!expanded && m_Model && !m_Selected.empty() &&
        m_Model->GetDefinitions().at(m_Model->Find(m_Selected)).m_Category == category)
    {
        EndEdit();
    }
    if (!self.Get())
    {
        return;
    }
    if (expanded)
    {
        m_Collapsed.erase(category);
    }
    else
    {
        m_Collapsed.insert(std::move(category));
    }
    RebuildRows();
}

bool PropertyGrid::IsCategoryExpanded(std::string_view category) const
{
    VerifyAccess();
    return !m_Collapsed.contains(std::string(category));
}

void PropertyGrid::SetEditorFactory(PropertyKind kind, PropertyEditorFactory factory)
{
    VerifyStructuralMutation();
    if (factory)
    {
        m_Factories[kind] = std::move(factory);
    }
    else
    {
        m_Factories.erase(kind);
    }
}

Widget* PropertyGrid::GetActiveEditor() const
{
    VerifyAccess();
    return m_Editor.Get();
}

const std::string& PropertyGrid::GetEditError() const
{
    VerifyAccess();
    return m_Error;
}

Rect PropertyGrid::GetPropertyBounds(std::string_view id) const
{
    VerifyAccess();
    if (!m_Model)
    {
        return {};
    }
    const auto index = static_cast<int>(m_Model->Find(id));
    const auto viewport = GetViewport();
    const auto offset = GetScrollOffset();
    const auto rowIndex = m_PropertyRows.at(static_cast<std::size_t>(index));
    if (rowIndex < m_Rows.size())
    {
        const auto& row = m_Rows[rowIndex];
        return {viewport.m_X, viewport.m_Y + row.m_Top - offset.m_Y, viewport.m_Width, row.m_Height};
    }
    return {};
}

Signal<const std::string&>& PropertyGrid::SelectionChanged()
{
    VerifyAccess();
    return m_SelectionChanged;
}

Signal<const std::string&>& PropertyGrid::EditRejected()
{
    VerifyAccess();
    return m_EditRejected;
}

bool PropertyGrid::IsFocusable() const
{
    VerifyAccess();
    return true;
}

void PropertyGrid::SetScrollOffset(Point offset)
{
    ScrollWidget::SetScrollOffset(offset);
    if (m_Editor.Get())
    {
        Invalidate(Invalidation::Arrange | Invalidation::HitTest | Invalidation::Paint);
    }
}

Point PropertyGrid::Measure(FontFace&) const
{
    return {360, 320};
}

Point PropertyGrid::GetVirtualSize() const
{
    return {0, m_Extent};
}

void PropertyGrid::RebuildRows()
{
    m_Rows.clear();
    m_PropertyRows.clear();
    m_Extent = 0;
    if (m_Model)
    {
        const auto& definitions = m_Model->GetDefinitions();
        m_PropertyRows.assign(definitions.size(), std::numeric_limits<std::size_t>::max());
        std::vector<std::pair<std::string, std::vector<int>>> groups;
        std::unordered_map<std::string, std::size_t> groupIndex;
        for (std::size_t index = 0; index < definitions.size(); ++index)
        {
            const auto& category = definitions[index].m_Category;
            const auto [position, added] = groupIndex.emplace(category, groups.size());
            if (added)
            {
                groups.emplace_back(category, std::vector<int>{});
            }
            groups[position->second].second.push_back(static_cast<int>(index));
        }
        const auto append = [&](int index, const std::string& category, float height)
        {
            if (index >= 0)
            {
                m_PropertyRows[static_cast<std::size_t>(index)] = m_Rows.size();
            }
            m_Rows.push_back({index, category, m_Extent, height});
            m_Extent += height;
        };
        for (const auto& [category, indices] : groups)
        {
            if (!category.empty())
            {
                append(-1, category, 30);
            }
            if (m_Collapsed.contains(category))
            {
                continue;
            }
            for (const int index : indices)
            {
                const auto& definition = definitions[static_cast<std::size_t>(index)];
                append(index, category,
                       definition.m_Id == m_Selected && definition.m_Kind == PropertyKind::Vector ? 64.0f : 36.0f);
            }
        }
    }
    NotifySemanticsChanged();
    Invalidate(Invalidation::Content);
}

std::size_t PropertyGrid::RowAt(Point point) const
{
    const auto viewport = GetViewport();
    if (!viewport.Contains(point))
    {
        return m_Rows.size();
    }
    const float y = point.m_Y - viewport.m_Y + GetScrollOffset().m_Y;
    const auto found = std::upper_bound(m_Rows.begin(), m_Rows.end(), y,
                                        [](float coordinate, const Row& row) { return coordinate < row.m_Top; });
    if (found == m_Rows.begin())
    {
        return m_Rows.size();
    }
    const auto index = static_cast<std::size_t>(found - m_Rows.begin() - 1);
    return y < m_Rows[index].m_Top + m_Rows[index].m_Height ? index : m_Rows.size();
}

void PropertyGrid::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    if (!m_Model)
    {
        return;
    }
    const auto viewport = GetViewport();
    const auto clip = viewport.Intersect(GetClip());
    const auto offset = GetScrollOffset();
    const auto& definitions = m_Model->GetDefinitions();
    const auto values = m_Model->Values().GetSnapshot();
    auto row = std::lower_bound(m_Rows.begin(), m_Rows.end(), offset.m_Y,
                                [](const Row& entry, float y) { return entry.m_Top + entry.m_Height <= y; });
    for (; row != m_Rows.end() && row->m_Top < offset.m_Y + viewport.m_Height; ++row)
    {
        const Rect bounds{viewport.m_X, viewport.m_Y + row->m_Top - offset.m_Y, viewport.m_Width, row->m_Height};
        if (row->m_Property < 0)
        {
            const auto& style =
                GetPartStyle("category", m_Collapsed.contains(row->m_Category) ? 0u : static_cast<unsigned>(Expanded));
            renderer.DrawBox(bounds, style, clip, opacity);
            DrawRowText(renderer, font, (m_Collapsed.contains(row->m_Category) ? "+  " : "-  ") + row->m_Category,
                        bounds, style, clip, opacity);
            continue;
        }
        const auto index = static_cast<std::size_t>(row->m_Property);
        const auto& property = definitions[index];
        const bool selected = property.m_Id == m_Selected;
        const unsigned state = (selected ? Selected : 0u) | (index % 2 ? Alternate : 0u) |
                               (property.m_ReadOnly ? ReadOnly : 0u) | (selected && !m_Error.empty() ? Invalid : 0u);
        const auto& style = GetPartStyle("row", state);
        renderer.DrawBox(bounds, style, clip, opacity);
        const float split = bounds.m_Width * 0.42f;
        DrawRowText(renderer, font, property.m_Label, {bounds.m_X, bounds.m_Y, split, bounds.m_Height}, style, clip,
                    opacity);
        if (!selected || !m_Editor.Get())
        {
            DrawRowText(renderer, font, FormatPropertyValue(values->at(index)),
                        {bounds.m_X + split, bounds.m_Y, bounds.m_Width - split, bounds.m_Height}, style, clip,
                        opacity);
        }
    }
    PaintScrollBars(renderer, opacity);
}

void PropertyGrid::OnPointerDown(const PointerEvent& event)
{
    if (HandleScrollPointerDown(event) || !m_Model)
    {
        return;
    }
    const auto index = RowAt(event.m_Position);
    if (index >= m_Rows.size())
    {
        return;
    }
    const auto row = m_Rows[index];
    if (row.m_Property < 0)
    {
        SetCategoryExpanded(row.m_Category, !IsCategoryExpanded(row.m_Category));
    }
    else
    {
        SelectProperty(m_Model->GetDefinitions().at(static_cast<std::size_t>(row.m_Property)).m_Id, true);
    }
}

void PropertyGrid::OnKey(Key key, bool down, bool, Modifiers)
{
    if (!down || !m_Model)
    {
        return;
    }
    if (key == Key::Enter || key == Key::Space)
    {
        BeginEdit();
    }
    else if (key == Key::Escape)
    {
        EndEdit();
    }
    else if (key == Key::Up || key == Key::Down)
    {
        std::vector<std::string> visible;
        for (const auto& row : m_Rows)
        {
            if (row.m_Property >= 0)
            {
                visible.push_back(m_Model->GetDefinitions().at(static_cast<std::size_t>(row.m_Property)).m_Id);
            }
        }
        if (visible.empty())
        {
            return;
        }
        const auto found = std::find(visible.begin(), visible.end(), m_Selected);
        const auto current = found == visible.end() ? -1 : static_cast<int>(found - visible.begin());
        const auto next = std::clamp(current + (key == Key::Down ? 1 : -1), 0, static_cast<int>(visible.size()) - 1);
        SelectProperty(visible[static_cast<std::size_t>(next)]);
    }
}

void PropertyGrid::OnLayout()
{
    if (auto* editor = m_Editor.Get())
    {
        auto bounds = GetPropertyBounds(m_Selected);
        const float split = bounds.m_Width * 0.42f;
        bounds.m_X += split;
        bounds.m_Width = std::max(0.0f, bounds.m_Width - split);
        editor->SetFloatingBounds(bounds.Intersect(GetViewport()).Intersect(GetClip()));
    }
}

void PropertyGrid::EndEdit()
{
    Detail::PropertySyncScope synchronizing(*this, m_Syncing);
    auto* editor = m_Editor.Get();
    m_Editor = {};
    const WidgetRef self(this);
    if (editor)
    {
        RemoveChild(*editor);
    }
    if (self.Get())
    {
        synchronizing.Reset();
    }
}

void PropertyGrid::BeginEdit()
{
    if (!m_Model || m_Selected.empty() || m_Editor.Get())
    {
        return;
    }
    const auto property = m_Model->GetDefinitions().at(m_Model->Find(m_Selected));
    if (property.m_ReadOnly)
    {
        return;
    }
    const auto value = m_Model->GetValue(m_Selected);
    const WidgetRef self(this);
    const auto model = m_Model;
    const auto commit = [self, model, id = property.m_Id](PropertyValue candidate)
    {
        if (auto* live = dynamic_cast<PropertyGrid*>(self.Get());
            live && live->m_Model == model && !live->m_Syncing && live->m_Selected == id)
        {
            live->Commit(id, std::move(candidate));
        }
    };
    std::unique_ptr<Widget> editor;
    const auto custom = m_Factories.find(property.m_Kind);
    if (custom != m_Factories.end())
    {
        editor = custom->second(property, value, commit);
    }
    else
    {
        switch (property.m_Kind)
        {
        case PropertyKind::Boolean:
        {
            auto field = std::make_unique<CheckBox>();
            field->SetChecked(std::get<bool>(value));
            field->Toggled().Connect([commit](bool candidate) { commit(candidate); });
            editor = std::move(field);
            break;
        }
        case PropertyKind::Number:
        {
            auto field = std::make_unique<UnitEditor>(std::get<double>(value));
            field->SetRange(property.m_Minimum, property.m_Maximum);
            field->SetUnits(property.m_Units);
            field->SetPrecision(property.m_Step, property.m_Decimals);
            field->ValueChanged().Connect([commit](double candidate) { commit(candidate); });
            editor = std::move(field);
            break;
        }
        case PropertyKind::String:
        {
            auto field = std::make_unique<TextBox>(std::get<std::string>(value));
            field->SetMaxBytes(65536);
            const WidgetRef text(field.get());
            field->Submitted().Connect([commit](const std::string& candidate) { commit(candidate); });
            field->FocusLost().Connect(
                [commit, text]
                {
                    if (const auto* live = dynamic_cast<TextBox*>(text.Get()))
                    {
                        commit(live->GetText());
                    }
                });
            editor = std::move(field);
            break;
        }
        case PropertyKind::Choice:
        {
            auto field = std::make_unique<ComboBox>();
            field->SetItems(property.m_Choices);
            const auto found =
                std::find(property.m_Choices.begin(), property.m_Choices.end(), std::get<std::string>(value));
            field->SetCurrentIndex(static_cast<int>(found - property.m_Choices.begin()));
            field->CurrentChanged().Connect(
                [commit, choices = property.m_Choices](int index)
                {
                    if (index >= 0 && static_cast<std::size_t>(index) < choices.size())
                    {
                        commit(choices[static_cast<std::size_t>(index)]);
                    }
                });
            editor = std::move(field);
            break;
        }
        case PropertyKind::Color:
        {
            auto field = std::make_unique<ColorEditor>();
            field->SetColor(std::get<Color>(value));
            field->ColorChanged().Connect([commit](Color candidate) { commit(candidate); });
            editor = std::move(field);
            break;
        }
        case PropertyKind::Vector:
        {
            auto field = std::make_unique<VectorEditor>(std::get<VectorValue>(value).size());
            field->SetValue(std::get<VectorValue>(value));
            field->SetRange(property.m_Minimum, property.m_Maximum);
            field->SetPrecision(property.m_Step, property.m_Decimals);
            field->ValueChanged().Connect([commit](const VectorValue& candidate) { commit(candidate); });
            editor = std::move(field);
            break;
        }
        }
    }
    if (!self.Get() || !editor || m_Model != model || m_Selected != property.m_Id)
    {
        return;
    }
    editor->AddClass("property-editor");
    editor->SetAccessibleName(property.m_Label);
    editor->SetToolTip(property.m_Help);
    editor->SetFloatingBounds({});
    auto& attached = AddChild(std::move(editor));
    m_Editor = WidgetRef(&attached);
    OnLayout();
    Widget* focus = &attached;
    if (auto* unitEditor = dynamic_cast<UnitEditor*>(&attached))
    {
        focus = &unitEditor->Editor().GetEditor();
    }
    else if (auto* vectorEditor = dynamic_cast<VectorEditor*>(&attached))
    {
        focus = &vectorEditor->Component(0).GetEditor();
    }
    else if (auto* colorEditor = dynamic_cast<ColorEditor*>(&attached))
    {
        focus = &colorEditor->Editor();
    }
    else if (auto* comboBox = dynamic_cast<ComboBox*>(&attached))
    {
        focus = &comboBox->GetEditor();
    }
    if (GetContext())
    {
        GetContext()->SetFocus(focus);
    }
}

void PropertyGrid::Commit(std::string id, PropertyValue value)
{
    const WidgetRef self(this);
    const auto model = m_Model;
    try
    {
        model->SetValue(id, std::move(value));
    }
    catch (const std::exception& error)
    {
        if (!self.Get() || m_Model != model)
        {
            return;
        }
        m_Error = error.what();
        SynchronizeEditor();
        if (!self.Get())
        {
            return;
        }
        const auto message = m_Error;
        SetToolTip(message);
        RequestRepaint();
        m_EditRejected.Emit(message);
        return;
    }
    if (self.Get() && m_Model == model)
    {
        m_Error.clear();
        RequestRepaint();
    }
}

void PropertyGrid::SynchronizeEditor()
{
    auto* editor = m_Editor.Get();
    if (!editor || !m_Model || m_Selected.empty() || m_Syncing)
    {
        return;
    }
    const auto value = m_Model->GetValue(m_Selected);
    const WidgetRef self(this);
    Detail::PropertySyncScope synchronizing(*this, m_Syncing);
    if (auto* checkBox = dynamic_cast<CheckBox*>(editor))
    {
        checkBox->SetChecked(std::get<bool>(value));
    }
    else if (auto* unitEditor = dynamic_cast<UnitEditor*>(editor))
    {
        unitEditor->SetValue(std::get<double>(value));
    }
    else if (auto* textBox = dynamic_cast<TextBox*>(editor))
    {
        textBox->SetText(std::get<std::string>(value));
    }
    else if (auto* comboBox = dynamic_cast<ComboBox*>(editor))
    {
        const auto choices = m_Model->GetDefinitions().at(m_Model->Find(m_Selected)).m_Choices;
        const auto found = std::find(choices.begin(), choices.end(), std::get<std::string>(value));
        comboBox->SetCurrentIndex(static_cast<int>(found - choices.begin()));
    }
    else if (auto* colorEditor = dynamic_cast<ColorEditor*>(editor))
    {
        colorEditor->SetColor(std::get<Color>(value));
    }
    else if (auto* vectorEditor = dynamic_cast<VectorEditor*>(editor))
    {
        vectorEditor->SetValue(std::get<VectorValue>(value));
    }
    if (self.Get())
    {
        synchronizing.Reset();
    }
}

SemanticInfo PropertyGrid::DescribeSemantics() const
{
    auto info = Widget::DescribeSemantics();
    info.m_Role = SemanticRole::Table;
    info.m_RowCount = m_Model ? m_Model->GetDefinitions().size() : 0;
    info.m_ColumnCount = 2;
    info.m_StructureRevision = m_SemanticGeneration;
    return info;
}

std::size_t PropertyGrid::GetSemanticChildCount(SemanticKey parent) const
{
    if (!m_Model)
    {
        return 0;
    }
    if (!parent.m_Item)
    {
        return m_Model->GetDefinitions().size();
    }
    return parent.m_Generation == m_SemanticGeneration && parent.m_Item <= m_Model->GetDefinitions().size() &&
                   !parent.m_Part
               ? 2
               : 0;
}

std::optional<SemanticKey> PropertyGrid::GetSemanticChildAt(SemanticKey parent, std::size_t index) const
{
    if (index >= GetSemanticChildCount(parent))
    {
        return {};
    }
    if (!parent.m_Item)
    {
        return SemanticKey{index + 1, 0, m_SemanticGeneration};
    }
    return SemanticKey{parent.m_Item, index + 1, m_SemanticGeneration};
}

std::optional<std::size_t> PropertyGrid::GetSemanticChildIndex(SemanticKey key) const
{
    if (!m_Model || key.m_Generation != m_SemanticGeneration || !key.m_Item ||
        key.m_Item > m_Model->GetDefinitions().size() || key.m_Part > 2)
    {
        return {};
    }
    return key.m_Part ? static_cast<std::size_t>(key.m_Part - 1) : static_cast<std::size_t>(key.m_Item - 1);
}

std::optional<SemanticVirtualNode> PropertyGrid::DescribeSemanticChild(SemanticKey key) const
{
    if (!GetSemanticChildIndex(key))
    {
        return {};
    }
    const auto index = static_cast<std::size_t>(key.m_Item - 1);
    const auto& definition = m_Model->GetDefinitions()[index];
    const auto value = m_Model->GetValue(definition.m_Id);
    SemanticVirtualNode node;
    auto& info = node.m_Info;
    info.m_Role = key.m_Part ? SemanticRole::Cell : SemanticRole::Row;
    info.m_Name = definition.m_Label;
    info.m_Description = definition.m_Category + (definition.m_Help.empty() ? std::string{} : ": " + definition.m_Help);
    info.m_Value = key.m_Part == 1 ? definition.m_Label : FormatPropertyValue(value);
    info.m_ReadOnly = definition.m_ReadOnly || key.m_Part == 1;
    info.m_Selected = definition.m_Id == m_Selected && key.m_Part == 2;
    info.m_Row = index;
    if (key.m_Part)
    {
        info.m_Column = static_cast<std::size_t>(key.m_Part - 1);
    }
    info.m_StructureRevision = m_SemanticGeneration;
    info.m_Actions = SemanticAction::Select | SemanticAction::ScrollIntoView;
    if (key.m_Part == 2 && !definition.m_ReadOnly)
    {
        info.m_Actions = info.m_Actions | SemanticAction::Invoke;
        if (definition.m_Kind == PropertyKind::Number)
        {
            info.m_Number = std::get<double>(value);
            info.m_Minimum = definition.m_Minimum;
            info.m_Maximum = definition.m_Maximum;
            info.m_Step = definition.m_Step;
            info.m_Actions = info.m_Actions | SemanticAction::SetNumber;
        }
        else if (definition.m_Kind == PropertyKind::Boolean)
        {
            info.m_Checked = std::get<bool>(value);
            info.m_Actions = info.m_Actions | SemanticAction::Toggle;
        }
        else if (definition.m_Kind != PropertyKind::Vector)
        {
            info.m_Actions = info.m_Actions | SemanticAction::SetText;
        }
    }
    node.m_Focusable = key.m_Part == 2;
    node.m_Focused = info.m_Selected;
    node.m_Bounds = GetPropertyBounds(definition.m_Id);
    if (key.m_Part)
    {
        node.m_Parent = {key.m_Item, 0, key.m_Generation};
        const float split = node.m_Bounds.m_Width * .42f;
        if (key.m_Part == 1)
        {
            node.m_Bounds.m_Width = split;
        }
        else
        {
            node.m_Bounds.m_X += split;
            node.m_Bounds.m_Width -= split;
        }
    }
    node.m_Bounds = node.m_Bounds.Intersect(GetViewport());
    return node;
}

SemanticStatus PropertyGrid::OnSemanticChildAction(SemanticKey key, const SemanticCommand& command)
{
    if (!GetSemanticChildIndex(key))
    {
        return SemanticStatus::Unavailable;
    }
    const auto model = m_Model;
    const auto definition = model->GetDefinitions()[static_cast<std::size_t>(key.m_Item - 1)];
    if (command.m_Action == SemanticAction::Select || command.m_Action == SemanticAction::ScrollIntoView)
    {
        SelectProperty(definition.m_Id);
        return SemanticStatus::Succeeded;
    }
    if (key.m_Part != 2 || definition.m_ReadOnly)
    {
        return SemanticStatus::Unsupported;
    }
    if (command.m_Action == SemanticAction::Invoke)
    {
        SelectProperty(definition.m_Id, true);
        return SemanticStatus::Succeeded;
    }
    try
    {
        if (command.m_Action == SemanticAction::SetNumber && definition.m_Kind == PropertyKind::Number)
        {
            model->SetValue(definition.m_Id, command.m_Number);
        }
        else if (command.m_Action == SemanticAction::Toggle && definition.m_Kind == PropertyKind::Boolean)
        {
            model->SetValue(definition.m_Id, !std::get<bool>(model->GetValue(definition.m_Id)));
        }
        else if (command.m_Action == SemanticAction::SetText && definition.m_Kind == PropertyKind::Color)
        {
            model->SetValue(definition.m_Id, ColorFromHex(command.m_Text));
        }
        else if (command.m_Action == SemanticAction::SetText &&
                 (definition.m_Kind == PropertyKind::String || definition.m_Kind == PropertyKind::Choice))
        {
            model->SetValue(definition.m_Id, command.m_Text);
        }
        else
        {
            return SemanticStatus::Unsupported;
        }
    }
    catch (const std::exception&)
    {
        return SemanticStatus::InvalidValue;
    }
    return SemanticStatus::Succeeded;
}

std::size_t PropertyGrid::GetSemanticSelectionCount() const
{
    return m_Model && !m_Selected.empty() ? 1 : 0;
}

std::vector<SemanticKey> PropertyGrid::GetSemanticSelection(std::size_t offset, std::size_t limit) const
{
    if (offset || !limit || !GetSemanticSelectionCount())
    {
        return {};
    }
    return {{m_Model->Find(m_Selected) + 1, 2, m_SemanticGeneration}};
}

std::optional<SemanticKey> PropertyGrid::GetSemanticGridItem(std::size_t row, std::size_t column) const
{
    if (!m_Model || row >= m_Model->GetDefinitions().size() || column >= 2)
    {
        return {};
    }
    return SemanticKey{row + 1, column + 1, m_SemanticGeneration};
}

std::optional<SemanticKey> PropertyGrid::GetSemanticFocus() const
{
    if (!GetSemanticSelectionCount())
    {
        return {};
    }
    return SemanticKey{m_Model->Find(m_Selected) + 1, 2, m_SemanticGeneration};
}

std::optional<SemanticKey> PropertyGrid::SemanticHitTest(Point point) const
{
    const auto row = RowAt(point);
    if (row >= m_Rows.size() || m_Rows[row].m_Property < 0)
    {
        return {};
    }
    const auto bounds = GetViewport();
    return SemanticKey{static_cast<std::uint64_t>(m_Rows[row].m_Property) + 1,
                       point.m_X < bounds.m_X + bounds.m_Width * .42f ? 1u : 2u, m_SemanticGeneration};
}
} // namespace EStudioUI
