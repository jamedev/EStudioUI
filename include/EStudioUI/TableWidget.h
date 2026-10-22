#pragma once

#include "ScrollWidget.h"
#include "TextBox.h"
#include "CollectionView.h"
#include "SortFilterModel.h"
#include "ItemDelegate.h"

namespace EStudioUI
{
//! Selects explicit content-column measurement cost.
enum class ContentMeasurePolicy
{
    Hints,
    Visible,
    Sample,
    Exact
};

//! A caption and explicit sizing contract for one table column.
struct TableColumn
{
    //! UTF-8 header caption.
    std::string m_Title;

    //! Fixed, content, or weighted Fill column sizing.
    SizeRule m_Size = SizeRule::Fill(1, 60);
};

//! A virtualized single-cell-selection table with sorting, resizing, and editing.
class TableWidget final : public ScrollWidget
{
  public:
    //! Creates an empty editable table.
    TableWidget();

    //! Displays shared typed model records; the table must belong to an idle UiContext.
    void SetModel(std::shared_ptr<ItemModel> model);

    //! Returns the external shared model, or an empty pointer for convenience-owned rows.
    std::shared_ptr<ItemModel> GetModel() const;

    //! Shares canonical selection with compatible collection views.
    void SetSelectionModel(std::shared_ptr<SelectionModel> selection);

    //! Returns shared selection when an external model is attached.
    std::shared_ptr<SelectionModel> GetSelectionModel() const;

    //! Returns the current canonical item ID, including filtered-out selection.
    ItemId GetCurrentItemId() const;

    //! Selects a canonical identity and schema column.
    void SetCurrentItemId(ItemId item, std::size_t column = 0);

    //! Selects bounded or exact content-width measurement; shared models default to Sample.
    void SetContentMeasurePolicy(ContentMeasurePolicy policy, std::size_t sampleCount = 64);

    //! Measures every row once explicitly and retains the resulting width cache until invalidation.
    void FitColumnsToContents();

    //! Installs an immutable per-column display/paint/editor policy; null restores the default.
    void SetItemDelegate(std::size_t column, std::shared_ptr<const ItemDelegate> delegate);

    //! Returns the active per-column delegate or the shared default implementation.
    std::shared_ptr<const ItemDelegate> GetItemDelegate(std::size_t column) const;

    //! Returns the most recent rejected editor commit, or empty text after a successful edit.
    const std::string& GetEditError() const;

    //! Reports rejected edits after preserving canonical data and the editable input.
    Signal<const std::string&>& EditRejected();

    //! Replaces columns, clears rows, and cancels any active editor.
    void SetColumns(std::vector<TableColumn> columns);

    //! Replaces rectangular row data; mismatched column counts throw atomically.
    void SetRows(std::vector<std::vector<std::string>> rows);

    //! Appends one rectangular row and returns its index.
    int AddRow(std::vector<std::string> cells);

    //! Returns the current row count.
    int GetRowCount() const;

    //! Returns the column count.
    int GetColumnCount() const;

    //! Returns a cell's UTF-8 value, throwing for invalid indices.
    std::string GetCell(int row, int column) const;

    //! Changes one value and emits CellChanged after the model is updated.
    void SetCell(int row, int column, std::string text);

    //! Selects a cell and reveals it; (-1,-1) clears selection.
    void SetCurrentCell(int row, int column);

    //! Returns selected (row,column), or (-1,-1).
    std::pair<int, int> GetCurrentCell() const;

    //! Stable-sorts rows lexicographically by UTF-8 value and preserves selected row identity.
    void SortByColumn(int column, bool ascending = true);

    //! Enables or disables user editing; disabling cancels an active editor.
    void SetEditable(bool editable);

    //! Enables or disables header-click sorting.
    void SetSortingEnabled(bool enabled);

    //! Overrides one column with a fixed width, including header-drag resizing.
    void SetColumnWidth(int column, float pixels);

    //! Assigns a Fixed/Content/Fill contract without replacing the attached model or its records.
    void SetColumnSize(int column, SizeRule size);

    //! Returns current allocated column widths.
    std::vector<float> GetColumnWidths() const;

    //! Returns a cell rectangle in window coordinates.
    Rect GetCellBounds(int row, int column) const;

    //! Opens a reusable text editor for a cell and selects its current contents.
    void BeginEdit(int row, int column);

    //! Commits an active editor and restores table focus.
    void CommitEdit();

    //! Cancels an active editor and restores table focus.
    void CancelEdit();

    //! Reports a committed cell value change.
    Signal<int, int>& CellChanged();

    //! Reports a committed selected-cell change.
    Signal<int, int>& CurrentCellChanged();

    //! Participates in Tab navigation.
    bool IsFocusable() const override;

    //! Excludes the fixed header from the scrolling body viewport.
    Rect GetViewport() const override;

  protected:
    //! Describes selection and logical structure without materializing row widgets.
    SemanticInfo DescribeSemantics() const override;

    //! Returns a direct logical child count.
    std::size_t GetSemanticChildCount(SemanticKey parent) const override;

    //! Resolves a logical child by position without scanning earlier records.
    std::optional<SemanticKey> GetSemanticChildAt(SemanticKey parent, std::size_t index) const override;

    //! Describes a stable live item using prepared geometry.
    std::optional<SemanticVirtualNode> DescribeSemanticChild(SemanticKey key) const override;

    //! Resolves a stable item's current sibling position.
    std::optional<std::size_t> GetSemanticChildIndex(SemanticKey key) const override;

    //! Applies selection, expansion, realization, or editing through canonical paths.
    SemanticStatus OnSemanticChildAction(SemanticKey key, const SemanticCommand& command) override;

    //! Resolves the logical item under one layout-coordinate point.
    std::optional<SemanticKey> SemanticHitTest(Point layoutPoint) const override;

    //! Returns the canonical selected set size without copying it.
    std::size_t GetSemanticSelectionCount() const override;

    //! Returns a bounded canonical selection page, including temporarily filtered items.
    std::vector<SemanticKey> GetSemanticSelection(std::size_t offset, std::size_t limit) const override;

    //! Returns the current stable item for logical keyboard focus.
    std::optional<SemanticKey> GetSemanticFocus() const override;

    //! Supplies a bounded preferred viewport size.
    Point Measure(FontFace& font) const override;

    //! Returns total allocated column width and virtual row height.
    Point GetVirtualSize() const override;

    //! Paints visible cells, fixed headers, selection, grid, and scrollbars.
    void Paint(Renderer& renderer, FontFace& font, double time, float opacity) const override;

    //! Repositions an active cell editor after resize or scrolling.
    void OnLayout() override;

    //! Selects/edits cells, sorts headers, or begins a column/scrollbar drag.
    void OnPointerDown(const PointerEvent& event) override;

    //! Updates column resizing and scrollbar dragging.
    void OnPointerMove(const PointerEvent& event) override;

    //! Ends column resizing and scrollbar dragging.
    void OnPointerUp(const PointerEvent& event) override;

    //! Cancels all captured drags.
    void OnCaptureLost() override;

    //! Navigates cells, copies values, and starts editing with Enter/F2.
    void OnKey(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Commits on Tab and cancels on Escape inside the embedded editor.
    bool OnKeyPreview(Key key, bool down, bool repeat, Modifiers modifiers) override;

    //! Starts editing with typed replacement text.
    void OnText(std::string_view text) override;

    //! Commits an active editor before scrolling the table body.
    bool OnScroll(Point delta, Modifiers modifiers) override;

    //! Resolves one grid cell directly by current row and schema column.
    std::optional<SemanticKey> GetSemanticGridItem(std::size_t row, std::size_t column) const override;

  private:
    //! Creates a collision-free record key in the current data namespace.
    SemanticKey SemanticRowKey(std::size_t row, std::uint64_t part = 0) const;

    //! Resolves a key into current visible row coordinates, or empty when filtered/deleted.
    std::optional<std::size_t> SemanticRowIndex(SemanticKey key) const;

    //! Prepared column prefix coordinates; semantic inspection never triggers content fitting.
    std::vector<float> m_SemanticColumnOffsets;

    //! Identifies convenience-owned rows independently of sorting and removal.
    std::vector<ItemId> m_SemanticRowIds;

    //! Resolves convenience-owned row identities in constant expected work.
    std::unordered_map<ItemId, std::size_t> m_SemanticRowIndices;

    //! Expires convenience dataset keys on complete replacement.
    std::uint64_t m_SemanticGeneration = AllocateItemId();

    //! Changes only when logical record identities or their ordering change.
    std::uint64_t m_SemanticStructureRevision = 1;

    //! Applies selection modifiers using stable IDs and current view order.
    void SelectCell(int row, int column, Modifiers modifiers);

    //! Invalidates relevant caches and remaps the active editor by stable identity.
    void ModelChanged(const ModelChange& change);

    //! Maps shared current identity and column into local coordinates.
    void SelectionChanged();

    //! Owns shared model/selection subscriptions without owning records per view.
    std::unique_ptr<CollectionView> m_View;

    //! Owns the table's private sort projection after header sorting is requested.
    std::shared_ptr<SortFilterModel> m_LocalProjection;

    //! Selects content-width measurement cost.
    ContentMeasurePolicy m_ContentPolicy = ContentMeasurePolicy::Exact;

    //! Bounds sampled content-column queries.
    std::size_t m_ContentSampleCount = 64;

    //! Forces one explicit exact width computation without changing the ordinary policy.
    mutable bool m_FitAll = false;

    //! Records the viewport range used by the Visible width policy.
    mutable std::size_t m_ContentFirstVisible = 0;

    //! Records the viewport row budget used by the Visible width policy.
    mutable std::size_t m_ContentVisibleCount = 0;

    //! Stores optional immutable delegates by schema column.
    std::vector<std::shared_ptr<const ItemDelegate>> m_Delegates;

    //! Holds the canonical record currently being edited.
    ItemId m_EditItem = 0;

    //! Detects conflicting external changes to the edited field.
    ModelValue m_EditOriginal;

    //! Prevents focus/model callbacks from recursively committing the same editor.
    bool m_Committing = false;

    //! Stores an inspectable validation or editing-conflict diagnostic.
    std::string m_EditError;

    //! Reports user-origin rejected edits without escaping through native event callbacks.
    Signal<const std::string&> m_EditRejected;

    //! Completes editing, optionally committing text and restoring focus.
    void EndEdit(bool commit, bool restoreFocus);

    //! Returns the column under a window-coordinate point, or -1.
    int ColumnAt(Point point) const;

    //! Returns the body row under a layout-coordinate point, or -1.
    int RowAt(Point point) const;

    //! Returns the fixed header under a layout-coordinate point, or -1.
    int HeaderAt(Point point) const;

    //! Stores headers and initial sizing contracts.
    std::vector<TableColumn> m_Columns;

    //! Stores rectangular UTF-8 model data.
    std::vector<std::vector<std::string>> m_Rows;

    //! Stores optional user-resized column widths.
    std::vector<std::optional<float>> m_Widths;

    //! Caches content-column preferred widths.
    mutable std::vector<float> m_ContentWidths;

    //! Records the font size associated with preferred column widths.
    mutable float m_ContentFontSize = -1;

    //! Records the header font used by content-column measurements.
    mutable float m_ContentHeaderFontSize = -1;

    //! Records the combined cell border and horizontal padding.
    mutable float m_ContentCellInset = -1;

    //! Records the combined header border and horizontal padding.
    mutable float m_ContentHeaderInset = -1;

    //! Stores the hovered body row, or -1.
    mutable int m_HoverRow = -1;

    //! Stores the hovered body column, or -1.
    mutable int m_HoverColumn = -1;

    //! Stores the hovered header, or -1.
    mutable int m_HoverHeader = -1;

    //! Remembers layout coordinates so hover follows cells while scrolling.
    Point m_HoverPosition;

    //! Stores the captured body row, or -1.
    int m_PressedRow = -1;

    //! Stores the captured body column, or -1.
    int m_PressedColumn = -1;

    //! Stores the captured header, or -1.
    int m_PressedHeader = -1;

    //! Stores the selected row.
    int m_CurrentRow = -1;

    //! Stores the selected column.
    int m_CurrentColumn = -1;

    //! Observes the owned, reusable floating cell editor.
    TextBox* m_Editor = nullptr;

    //! Stores the row currently being edited, or -1.
    int m_EditRow = -1;

    //! Stores the column currently being edited.
    int m_EditColumn = -1;

    //! Enables user editing.
    bool m_Editable = true;

    //! Enables header-click sorting.
    bool m_SortingEnabled = true;

    //! Stores the last sorted column, or -1.
    int m_SortColumn = -1;

    //! Stores the current sort direction.
    bool m_Ascending = true;

    //! Stores the column currently being resized, or -1.
    int m_ResizeColumn = -1;

    //! Stores the drag-start pointer coordinate.
    float m_ResizeOrigin = 0;

    //! Stores the drag-start column width.
    float m_ResizeWidth = 0;

    //! Delivers committed value changes.
    Signal<int, int> m_CellChanged;

    //! Delivers committed selected-cell changes.
    Signal<int, int> m_CurrentCellChanged;
};
} // namespace EStudioUI
