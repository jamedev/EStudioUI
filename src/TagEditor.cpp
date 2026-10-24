#include <EStudioUI/TagEditor.h>
#include <EStudioUI/FlowLayout.h>
#include <EStudioUI/UiContext.h>
#include "PropertyNotification.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace EStudioUI
{
TagEditor::TagEditor() : Widget("tageditor")
{
    m_Chips = &Add<Widget>("tagchips");
    m_Chips->SetLayout(FlowLayout{});
    m_Chips->SetWidth(SizeRule::Fill());
    auto& row = Add<Widget>();
    row.SetLayoutDirection(Axis::Horizontal);
    row.SetWidth(SizeRule::Fill());
    m_Editor = &row.Add<TextBox>();
    m_Editor->SetMaxBytes(256);
    m_Editor->SetPlaceholder("Add tag, then Enter");
    m_Editor->SetWidth(SizeRule::Fill());
    m_Choices = &row.Add<MultiSelectComboBox>();
    m_Choices->SetWidth(SizeRule::Fixed(150));
    m_Choices->SetVisible(false);
    const WidgetRef self(this);
    m_Editor->Submitted().Connect(
        [self](const std::string& text)
        {
            const auto value = text;
            if (auto* live = dynamic_cast<TagEditor*>(self.Get()))
            {
                live->AddTag(value);
                if ((live = dynamic_cast<TagEditor*>(self.Get())))
                {
                    live->m_Editor->SetText("");
                }
            }
        });
    m_Choices->SelectionChanged().Connect(
        [self](std::vector<ItemId> ids)
        {
            if (auto* live = dynamic_cast<TagEditor*>(self.Get()); live && !live->m_Syncing)
            {
                const auto snapshot = live->m_Choices->GetModel()->GetSnapshot();
                std::unordered_set<std::string> catalog;
                for (std::size_t index = 0; index < snapshot.GetRowCount(); ++index)
                {
                    catalog.insert(snapshot.GetText(snapshot.GetItem(index), 0));
                }
                std::vector<std::string> tags;
                for (const auto& tag : live->m_Tags)
                {
                    if (!catalog.contains(tag))
                    {
                        tags.push_back(tag);
                    }
                }
                for (const auto id : ids)
                {
                    tags.push_back(snapshot.GetText(id, 0));
                }
                live->SetTags(std::move(tags));
            }
        });
}

void TagEditor::SetTags(std::vector<std::string> tags)
{
    VerifyStructuralMutation();
    if (tags.size() > 128)
    {
        throw std::length_error("Tag editors accept at most 128 tags.");
    }
    std::unordered_set<std::string> unique;
    for (const auto& tag : tags)
    {
        if (tag.empty() || tag.size() > 256 || !unique.insert(tag).second)
        {
            throw std::invalid_argument("Tags must be unique, nonempty and at most 256 bytes.");
        }
    }
    if (m_Tags == tags)
    {
        return;
    }
    const WidgetRef self(this);
    const auto value = tags;
    m_Tags = std::move(tags);
    Rebuild();
    if (auto* live = dynamic_cast<TagEditor*>(self.Get()); live && live->m_Tags == value)
    {
        Detail::EmitPropertyNotification(*live, live->m_NotificationDepth, live->m_TagsChanged, value);
    }
}

const std::vector<std::string>& TagEditor::GetTags() const
{
    VerifyAccess();
    return m_Tags;
}

bool TagEditor::AddTag(std::string tag)
{
    VerifyStructuralMutation();
    if (tag.empty() || std::find(m_Tags.begin(), m_Tags.end(), tag) != m_Tags.end())
    {
        return false;
    }
    auto tags = m_Tags;
    tags.push_back(std::move(tag));
    SetTags(std::move(tags));
    return true;
}

bool TagEditor::RemoveTag(std::string_view tag)
{
    VerifyStructuralMutation();
    auto tags = m_Tags;
    const auto found = std::find(tags.begin(), tags.end(), tag);
    if (found == tags.end())
    {
        return false;
    }
    tags.erase(found);
    SetTags(std::move(tags));
    return true;
}

void TagEditor::Rebuild()
{
    if (GetContext() && m_Choices->IsVisible())
    {
        Detail::PropertySyncScope scope(*this, m_Syncing);
        const auto snapshot = m_Choices->GetModel()->GetSnapshot();
        std::vector<ItemId> selected;
        for (std::size_t row = 0; row < snapshot.GetRowCount(); ++row)
        {
            const auto id = snapshot.GetItem(row);
            if (std::find(m_Tags.begin(), m_Tags.end(), snapshot.GetText(id, 0)) != m_Tags.end())
            {
                selected.push_back(id);
            }
        }
        const WidgetRef self(this);
        m_Choices->SetSelectedItems(std::move(selected));
        if (!self.Get())
        {
            return;
        }
    }
    while (!m_Chips->GetChildren().empty())
    {
        m_Chips->RemoveChild(*m_Chips->GetChildren().back());
    }
    const WidgetRef self(this);
    for (const auto& tag : m_Tags)
    {
        auto& chip = m_Chips->Add<ToolButton>(tag + "  ×");
        chip.AddClass("tag-chip");
        chip.SetAccessibleName("Remove " + tag);
        chip.Clicked().Connect(
            [self, tag]
            {
                if (auto* live = dynamic_cast<TagEditor*>(self.Get()))
                {
                    live->RemoveTag(tag);
                }
            });
    }
}

void TagEditor::SetEditable(bool enabled)
{
    VerifyStructuralMutation();
    m_Editor->SetVisible(enabled);
}

void TagEditor::SetSuggestions(std::vector<std::string> suggestions)
{
    VerifyStructuralMutation();
    std::unordered_set<std::string> unique;
    for (const auto& tag : suggestions)
    {
        if (tag.empty() || tag.size() > 256 || !unique.insert(tag).second)
        {
            throw std::invalid_argument("Suggested tags must be unique, nonempty and at most 256 bytes.");
        }
    }
    {
        Detail::PropertySyncScope scope(*this, m_Syncing);
        m_Choices->SetItems(std::move(suggestions));
        m_Choices->SetVisible(true);
    }
    Rebuild();
}

TextBox& TagEditor::GetEditor() const
{
    VerifyAccess();
    return *m_Editor;
}

Signal<std::vector<std::string>>& TagEditor::TagsChanged()
{
    VerifyAccess();
    return m_TagsChanged;
}
} // namespace EStudioUI
