#pragma once

#include "ScrollWidget.h"
#include "TextBox.h"

namespace EStudioUI
{
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
    const std::string& GetCell(int row, int column) const;

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

  private:
    //! Completes editing, optionally committing text and restoring focus.
    void EndEdit(bool commit, bool restoreFocus);

    //! Returns the column under a window-coordinate point, or -1.
    int ColumnAt(Point point) const;

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
