#pragma once

#include "PropertyModel.h"
#include "ScrollWidget.h"

#include <set>

namespace EStudioUI
{
//! Builds one live editor; the callback accepts complete owned canonical candidates.
using PropertyEditorFactory = std::function<std::unique_ptr<Widget>(const PropertyDefinition&, const PropertyValue&,
                                                                    std::function<void(PropertyValue)>)>;

//! A virtual categorized property inspector retaining only one active editor subtree.
class PropertyGrid : public ScrollWidget
{
  public:
    //! Creates an empty inspector with keyboard and shared scrollbar navigation.
    PropertyGrid();

    //! Installs canonical owner-side state, retiring the previous editor and subscriptions.
    void SetModel(std::shared_ptr<PropertyModel> model);

    //! Returns the current canonical property model.
    std::shared_ptr<PropertyModel> GetModel() const;

    //! Selects and reveals a stable property ID, optionally opening its typed editor.
    void SelectProperty(std::string_view id, bool edit = false);

    //! Returns the selected stable ID, or empty.
    const std::string& GetSelectedProperty() const;

    //! Collapses a category without destroying canonical values.
    void SetCategoryExpanded(std::string category, bool expanded);

    //! Returns configured category expansion.
    bool IsCategoryExpanded(std::string_view category) const;

    //! Overrides a built-in editor policy; an empty factory restores its default.
    void SetEditorFactory(PropertyKind kind, PropertyEditorFactory factory);

    //! Returns the current editor, or null; ownership remains with the grid.
    Widget* GetActiveEditor() const;

    //! Returns the last rejected commit diagnostic.
    const std::string& GetEditError() const;

    //! Returns prepared row geometry for automation and visual tooling.
    Rect GetPropertyBounds(std::string_view id) const;

    //! Reports stable selection identities.
    Signal<const std::string&>& SelectionChanged();

    //! Reports rejected commits after preserving canonical state.
    Signal<const std::string&>& EditRejected();

    //! Participates in keyboard focus without allocating row widgets.
    bool IsFocusable() const override;

    //! Keeps a live editor aligned with its virtual row when scrolling.
    void SetScrollOffset(Point offset) override;

  protected:
    //! Describes a virtual two-column table without materializing rows.
    SemanticInfo DescribeSemantics() const override;

    //! Pages immutable schema rows and their two cells directly.
    std::size_t GetSemanticChildCount(SemanticKey parent) const override;

    //! Resolves a direct schema coordinate in constant work.
    std::optional<SemanticKey> GetSemanticChildAt(SemanticKey parent, std::size_t index) const override;

    //! Describes canonical values using prepared row geometry.
    std::optional<SemanticVirtualNode> DescribeSemanticChild(SemanticKey key) const override;

    //! Resolves stable schema positions without scanning virtual rows.
    std::optional<std::size_t> GetSemanticChildIndex(SemanticKey key) const override;

    //! Applies canonical property edits, selection and realization.
    SemanticStatus OnSemanticChildAction(SemanticKey key, const SemanticCommand& command) override;

    //! Resolves a prepared visible value or label cell under a pointer.
    std::optional<SemanticKey> SemanticHitTest(Point point) const override;

    //! Returns the zero-or-one selected schema row.
    std::size_t GetSemanticSelectionCount() const override;

    //! Returns a bounded selection page without copying the schema.
    std::vector<SemanticKey> GetSemanticSelection(std::size_t offset, std::size_t limit) const override;

    //! Resolves one schema row/cell identity directly.
    std::optional<SemanticKey> GetSemanticGridItem(std::size_t row, std::size_t column) const override;

    //! Reports the stable selected logical value cell.
    std::optional<SemanticKey> GetSemanticFocus() const override;

    //! Returns a bounded natural inspector size.
    Point Measure(FontFace& font) const override;

    //! Returns cached row extent in constant time.
    Point GetVirtualSize() const override;

    //! Paints visible rows only with cached shaping and precompiled part styles.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

    //! Selects rows, toggles categories and preserves shared scrollbar dragging.
    void OnPointerDown(const PointerEvent& event) override;

    //! Navigates rows and opens editors with Enter or Space.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Positions the one active editor without running an event loop.
    void OnLayout() override;

  private:
    //! One virtual row with a stable schema coordinate or category header.
    struct Row
    {
        //! Property index; -1 identifies a category.
        int m_Property = -1;

        //! Owned category label.
        std::string m_Category;

        //! Cached virtual vertical geometry.
        float m_Top = 0, m_Height = 32;
    };

    //! Rebuilds bounded row metadata after schema or expansion changes.
    void RebuildRows();

    //! Creates one typed editor for the selected canonical property.
    void BeginEdit();

    //! Retires the editor without dereferencing it after callbacks.
    void EndEdit();

    //! Commits a candidate through State validation, retaining useful rejection text.
    void Commit(std::string id, PropertyValue value);

    //! Mirrors external canonical updates into the current built-in editor.
    void SynchronizeEditor();

    //! Finds a visible row at a layout coordinate in logarithmic time.
    std::size_t RowAt(Point point) const;

    //! Observes canonical data with shared ownership.
    std::shared_ptr<PropertyModel> m_Model;

    //! Retires model subscriptions before releasing canonical data.
    std::vector<ScopedConnection> m_Connections;

    //! Caches virtual rows independently of painting.
    std::vector<Row> m_Rows;

    //! Maps immutable property coordinates to prepared row positions in constant work.
    std::vector<std::size_t> m_PropertyRows;

    //! Distinguishes schema replacement even when property indices repeat.
    std::uint64_t m_SemanticGeneration = 1;

    //! Tracks collapsed categories.
    std::set<std::string> m_Collapsed;

    //! Holds optional application editor factories.
    std::map<PropertyKind, PropertyEditorFactory> m_Factories;

    //! Stable selected property identity.
    std::string m_Selected;

    //! Observes the sole active editor subtree.
    WidgetRef m_Editor;

    //! Reports rejected canonical commits.
    std::string m_Error;

    //! Suppresses editor synchronization feedback and stale teardown callbacks.
    bool m_Syncing = false;

    //! Total cached virtual height.
    float m_Extent = 0;

    //! Reports stable selection changes.
    Signal<const std::string&> m_SelectionChanged;

    //! Reports diagnostic rejection text.
    Signal<const std::string&> m_EditRejected;
};
} // namespace EStudioUI
