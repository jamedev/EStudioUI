#pragma once
#include "MultiSelectComboBox.h"
#include "TextBox.h"

namespace EStudioUI
{
//! Bounded removable tags with optional free-form entry and a reusable multiple-choice selector.
class TagEditor : public Widget
{
  public:
    //! Creates an empty free-form tag editor.
    TagEditor();
    //! Replaces at most 128 unique nonempty tags in one publication.
    void SetTags(std::vector<std::string> tags);
    //! Returns the owned current tags.
    const std::vector<std::string>& GetTags() const;
    //! Adds one unique tag; returns false for an empty or duplicate value.
    bool AddTag(std::string tag);
    //! Removes one exact tag value.
    bool RemoveTag(std::string_view tag);
    //! Enables or disables free-form entry.
    void SetEditable(bool enabled);
    //! Sets the optional reusable choice catalog.
    void SetSuggestions(std::vector<std::string> suggestions);
    //! Returns the text editor for validators and IME.
    TextBox& GetEditor() const;
    //! Reports one owning complete tag set after a change.
    Signal<std::vector<std::string>>& TagsChanged();

  private:
    //! Reconciles the bounded composed chips outside painting.
    void Rebuild();
    //! Owned selected tag strings.
    std::vector<std::string> m_Tags;
    //! Flow container for bounded tag chips.
    Widget* m_Chips = nullptr;
    //! User-defined tag input.
    TextBox* m_Editor = nullptr;
    //! Optional catalog chooser.
    MultiSelectComboBox* m_Choices = nullptr;
    //! Suppresses circular chooser reconciliation.
    bool m_Syncing = false;
    //! Bounds synchronous observer feedback.
    unsigned m_NotificationDepth = 0;
    //! Complete-value observers.
    Signal<std::vector<std::string>> m_TagsChanged;
};
} // namespace EStudioUI
