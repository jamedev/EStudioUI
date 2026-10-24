#pragma once

#include "ModelValue.h"
#include "Widget.h"

namespace EStudioUI
{
//! Extends a virtual view with one reusable composed editor, never one widget per cell.
class CellEditor : public Widget
{
  public:
    //! Creates a styled editor composition with explicit acceptance/cancellation.
    CellEditor();

    //! Releases its owned controls through ordinary widget lifetime rules.

    ~CellEditor() override;

    //! Returns the exact proposed canonical value or throws a useful validation error.

    virtual ModelValue GetValue() const = 0;

    //! Returns the ordinary child that should receive keyboard focus.

    virtual Widget& FocusTarget() const;

    //! Requests a confirmed model edit without prescribing synchronous persistence.
    Signal<>& CommitRequested();

    //! Requests discarding only this uncommitted draft.
    Signal<>& CancelRequested();

  private:
    //! Reports explicit draft acceptance.
    Signal<> m_CommitRequested;

    //! Reports explicit draft cancellation.
    Signal<> m_CancelRequested;
};

//! Creates built-in bool/choice/numeric/color/text/binary editors from immutable schema metadata.
std::unique_ptr<CellEditor> CreateTypedCellEditor(const ModelColumn& column, const ModelValue& value);
} // namespace EStudioUI
