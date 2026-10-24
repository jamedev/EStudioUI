# Stronger data views and reusable source adapters (0.25)

The existing `ItemModel`, immutable `ModelSnapshot`, `SelectionModel` and
`SortFilterModel` remain the collection boundary. `TreeTable` extends the table's
virtual cell renderer with the expanded-branch row map. No widget is created for
a source record or an unedited cell.

## Common path

```cpp
#include <EStudioUI/AdapterModel.h>
#include <EStudioUI/SqliteAdapter.h>
#include <EStudioUI/TableWidget.h>

// application.Tasks() outlives this window's model. Construction queues discovery.
auto model = EStudioUI::AdapterModel::Create(
    window.Ui(), std::make_shared<EStudioUI::SqliteAdapter>(path), application.Tasks());
auto& table = window.Ui().Root().Add<EStudioUI::TableWidget>();
table.SetModel(model);
table.SetColumnsMovable(true);
```

Link `EStudioUI::Sqlite` for this provider. The core `EStudioUI::EStudioUI` target
contains the generic adapter/view APIs and does not link SQLite. Disable
`ESTUDIOUI_BUILD_SQLITE` and `ESTUDIOUI_BUILD_DATABASE_BROWSER` together for a
build without the SQLite target. The shared pinned manifest still lists its
package; per-module dependency manifests belong to the deferred F14 packaging work.

After catalog discovery, the bridge loads the first dataset. Use `GetCatalog()`
and `SubscribeStatus()` to build source selectors. An operation reports Catalog,
Loading, Committing or Idle. Status describes the **last published page**, its
query and its total/matching counts. Old rows remain intact while loading or after
a failed query. A status observer must not label them as a newly requested result.

## Implementing your own provider

Derive from `ModelAdapter`, not from a widget:

- `GetDataSets(stop)` returns owned IDs, schema/editor metadata and capabilities.
- `Load(query, stop)` returns one bounded page, coherent total/matching counts,
  the actual query and an opaque source key per record.
- Override `Commit(query, edits, stop)` for atomic optimistic edits. Each edit
  owns the original and proposed raw value. Return a page captured from the
  committed transaction; throw before success when the transaction failed.

Provider methods run on application workers, may overlap, and must cooperate with
cancellation. Do not capture GUI objects in provider metadata/callbacks. Formatting,
parsing and validation policies must be pure and safe on their documented callers.
The page contract is flat. Hierarchy views bind a hierarchical ItemModel directly,
as in the playground; lazy per-branch provider fetching is separate work.
The default `Commit` is read-only. `SnapshotModelAdapter` adapts existing immutable
C++ snapshots to the same query/page contract, flattening stored records. It deliberately performs a complete
worker-side filter/sort scan; it does not turn an in-memory scan into indexed I/O.

[examples/ModelAdapter.cpp](examples/ModelAdapter.cpp) is a compiled custom provider
example. [browser/DatabaseBrowserWindow.cpp](browser/DatabaseBrowserWindow.cpp) is
the working application consumer, including a separate shared in-memory hierarchy.

Source identity, schema revision and UI identity are distinct. Set `m_StableKeys`
false for a dataset without reliable keys; IDs then expire on refresh. Change
`m_SchemaRevision` when field coordinates or identity semantics change. A bridge
rejects mismatched query/schema results. Its bounded key cache retains at most
32,768 identities per dataset/schema domain; returning to an evicted page may
allocate new UI IDs. A refreshed SQLite schema expires the old domain.

## Query semantics and costs

`ModelQuery` owns a dataset, schema revision, filter tree, ordered sort keys,
offset and page size. Leaves accept typed equality, inequality, range comparison,
Contains, StartsWith, IsNull and IsNotNull. Groups combine AND/OR recursively.
An empty AND matches all; an empty OR matches none. Case-insensitive text search
folds ASCII consistently across the built-in providers; it is not Unicode locale
collation. Text search patterns are literal, including `%` and `_`.

Queries allow 1–2048 rows, at most 16 unique sort keys, 128 filter nodes and 16
nested groups. Schema coordinates remain stable when columns move visually.
Null placement is explicit per key. `SortFilterModel::SetSortKeys` provides the
same multi-key ordering for ordinary application snapshots. `CompileModelFilter`
validates and owns a predicate once for repeated reads from a compatible schema.

The SQLite provider binds values, quotes identifiers, removes implicit column
affinity from portable comparisons and explicitly controls collation/null order.
It performs counts and LIMIT/OFFSET reads in one transaction. Appropriate database
indexes help query execution; exact counts, unindexed predicates/sorts and deep
OFFSETs can still scan substantial data. No keyset-pagination or hardware timing
claim is made. Cancel stops SQLite VM work and busy waits cooperatively.

## Bounded presentation and selection

`AdapterModel` publishes **one loaded result window**, rather than pretending that
unloaded database rows are addressable in its snapshot. Counts distinguish loaded,
matching and total rows. `SetPage()` and the example's page controls load another
window. The table's scrollbar navigates that window. Selection is canonical within
loaded snapshots and is pruned when a page no longer contains the selected record;
this is not a cross-database persistent selection service.

The bridge validates bounded rectangular pages and final-candidate raw values.
It limits raw page publication to 32 MiB and presentation to 8 MiB. Worker-prepared
display strings are capped at 1024 UTF-8 bytes plus an ellipsis. Painting copies
bounded display values, while Edit roles retain exact raw data. Built-in SQLite
limits default to 1 MiB per field and 8 MiB per loaded page; oversized raw values
reject the load without truncating source data. Large-text/document editing remains
F10b. Inline binary/text editors deliberately accept smaller drafts (32/64 KiB).

A reserved per-request result slot and owner idle turn deliver source completion
even when the ordinary dispatcher queue is full. Superseded reads cannot replace
a newer request. Mutation admission rejects traversal/transaction use before I/O.
Worker completion retains only CPU inbox data, so it cannot move UI-state
destruction onto a worker during owner retirement. Dropping a model cancels
requests and settles sessions without joining the external
application executor. That executor and the context must outlive the model.

## Confirmed editing and extension

`ItemModel::RequestEdit` returns `ModelEditSession`. Ordinary models settle it
synchronously; adapter models settle after confirmed worker persistence and page
publication. A table keeps a disabled draft while pending, restores it on failure,
and closes it on success. Do not use synchronous `SetValue`/`SetCell` to write an
asynchronous adapter. Persistence rules belong in the backend transaction. A
presentation/publication failure can occur after persistence succeeded; the edit
then remains Committed with a diagnostic, while the bridge retains its old page
and reports an error until a successful refresh.
Derived models can implement `RequestEdit` using protected
`BeginEditSession` and `CompleteEditSession`; a session can outlive a view.

Set `ModelColumn::m_EditorType`, `m_Nullable` and/or `m_Choices` to use composed
editors: exact signed integers, numeric input, checkbox, combo choice, color,
text and hexadecimal binary. Nullable fields have an explicit NULL toggle; empty
text and empty binary stay distinct. SQLite Any fields follow their actual storage
type, with declared-type hints for NULL; declared BOOLEAN values 0/1 use a checkbox
while retaining integer storage. Nonstandard boolean values keep an integer editor.
Primary keys/generated columns are read-only. Database constraints remain final.

An immutable custom `ItemDelegate` may override `CreateEditor` with a `CellEditor`
composition, its exact `GetValue`, `FocusTarget`, and commit/cancel signals. The
legacy text editor remains the default for schemas without typed-editor metadata.
There is one active editor, never a cell widget per record. Accessibility SetText
routes through the same request API; pending success means accepted submission,
with later failures reported through `EditRejected`/adapter status.

## Columns, hierarchy and dragging

`SetColumnOrder`, `MoveColumn`, `SetColumnVisible`, `SetColumnSize` and existing
resize handles operate on schema indices. At least one column stays visible.
`GetColumnOrder(true)` provides visible order. Opt into header dragging with
`SetColumnsMovable(true)`. Header click sorting remains compatible with existing
code; connecting `SortRequested` delegates sorting to an application/server query.
Its third argument reports Shift for additive keys. `SetSortIndicators` reflects
the confirmed query. Keyboard navigation and accessible grid coordinates use the
visible order; cell identities retain schema coordinates.

`TreeTable::SetExpanded` is local presentation state. `RevealItem` expands
ancestors and selects a record. Branches have accessible Expand/Collapse actions.
Branch prefixes scale with expanded branches, not collapsed descendant count.
Content sizing stays bounded by Hints/Visible/Sample unless explicitly fitting all
visible rows. Expansion/structure changes rebuild branch prefixes, not cell widgets.

`SetRowsMovable(true)` enables captured row dragging in TableWidget, TreeTable,
ListWidget and TreeWidget. The insertion line or child outline uses the same GPU
renderer. A threshold distinguishes clicking from dragging; pressing an already
selected row retains a multi-selection until a click is confirmed. Default drops
move a stable-ID forest in the shared canonical model, reject cycles and stale
revisions, and preserve descendants. A drop is an explicit structural O(n) rebuild;
ordinary hover and painting do not rebuild the model.

Cross-view drops work in the same UiContext, respect transformed hit geometry,
clipping/modal input and share source identity. `SetRowDropHandler` supplies custom
copy/import/transfer policy with an owned `ModelDragData` snapshot and stable target.
`RowsDropped`/`DropRejected` report outcomes. OS/cross-window drags, drag-edge auto
scroll, structural database writes and persistent database ordering are separate
work; SQL result rows do not gain a fictitious physical order.

All tables inherit existing rich row/cell/header/scroll CSS. TreeTable additionally
registers `::branch` with `:expanded`. Typed editors compose ordinary styled
controls under `.cell-editor`, with `.cell-editor-value`, `.cell-null`,
`.cell-apply` and `.cell-cancel` hooks.
