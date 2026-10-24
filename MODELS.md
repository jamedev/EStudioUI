# Shared models, selection, and virtual collection views

The 0.25 [data-view and adapter extension](DATA_VIEWS.md) adds portable typed
queries, multi-key sorting, exact binary/null fields, confirmed asynchronous
edits, bounded source windows, TreeTable and captured canonical row moves.
[SQLITE_BROWSER.md](SQLITE_BROWSER.md) documents the working SQLite consumer.


EStudioUI 0.11 adds the first F06 model foundation. One `ItemModel` publishes
owned immutable data to several views. `SelectionModel` shares canonical item
identities; `SortFilterModel` supplies independent ordering and filtering.
The existing list, table, and tree CSS parts and GPU renderer remain in use.
No runtime dependency was added.

## A small complete application

Run this application from the demo executable's directory so its copied font is
available, or supply your own font path.

```cpp
#include <EStudioUI/Application.h>
#include <EStudioUI/ListWidget.h>
#include <EStudioUI/Models.h>
#include <EStudioUI/TableWidget.h>
#include <EStudioUI/Window.h>

#include <string>
#include <vector>

struct Part
{
    std::string m_Name;
    double m_Length = 0;
    bool m_Valid = true;
};

int main()
{
    using namespace EStudioUI;
    Application app;
    Window window(WindowOptions{}, "assets/NotoSans.ttf");
    auto& ui = window.Ui();
    ui.Root().SetLayoutDirection(Axis::Horizontal);
    auto& table = ui.Root().Add<TableWidget>();
    auto& list = ui.Root().Add<ListWidget>();
    table.SetWidth(SizeRule::Fill(2));
    list.SetWidth(SizeRule::Fill());
    table.SetHeight(SizeRule::Fill());
    list.SetHeight(SizeRule::Fill());

    auto model = MakeTableModel(ui.States(),
        std::vector<Part>{{"Line A", 20, true}, {"Arc B", 3, false}},
        Column("Name", &Part::m_Name),
        Column("Length", &Part::m_Length),
        Column("Valid", &Part::m_Valid));
    auto selection = SelectionModel::Create(model);
    table.SetModel(model);
    list.SetModel(model);
    table.SetSelectionModel(selection);
    list.SetSelectionModel(selection);

    auto sorted = SortFilterModel::Create(model);
    sorted->SetSort(1); // Numeric raw-value ordering: 3, then 20.
    table.SetModel(sorted); // Retains compatible canonical selection.
    return app.Run();
}
```

A view must already belong to an idle `UiContext` when `SetModel` connects it.
Views retain shared models and selection; subscription guards observe widgets
weakly. Removing a view disconnects its callbacks and phase guards. Models and
selection belong to their store's owner thread; captured `ModelSnapshot` values
can be read on workers. The store must remain alive for attached model operations.
Destroying it expires handles and diagnoses subsequent facade access.

## Data, identity, and ownership

`MakeTableModel` moves ordinary C++ records into immutable 256-record pages.
Typed adapters require copyable records. Columns use member accessors and retain raw `bool`, signed 64-bit integer,
`double`, text, or `Color` values. `ModelValueType` names those alternatives;
`ModelValueType::Any` permits mixed owned alternatives. Domain records remain
available as owned copies with `snapshot.GetRecord<Part>(item)` when the storage
adapter exposes that concrete type.

Every record has a nonzero `ItemId`; zero denotes an invisible hierarchy root or
no current item. Automatically allocated IDs are process-unique and never reused.
`MakeTableSnapshot(records, columns, ids, parents)` accepts application-provided
IDs and parent IDs. Those IDs must remain unique and represent the same entities
across revisions. A new canonical model is a separate selection domain even if
its numeric IDs match another model. Replacements preserve selection only for IDs
retained in the replacement.

A snapshot owns its storage. References to schema and advanced record addresses
remain tied to that snapshot's lifetime. Values and typed record copies are owned.
Keep UI/native resources outside records and callback captures: the last snapshot
reader, including a worker, can run their destructors. Custom getters, formatting,
validation, role callbacks, and predicates must be pure and safe for their actual
readers. Capture owned immutable settings when sharing them with workers.

## Changes and transactions

`SetValue(item, column, value)` edits a typed raw field. `SetText` parses editor
text through its column policy. `Apply(vector<ModelEdit>)` performs a large edit
batch: repeated edits to the same field use the final candidate, touched pages
are copied once, and unchanged pages/indexes remain shared. A page-directory copy
is proportional to page count; each touched page copies at most 256 records.

Use one `Apply` for thousands of edits. `StateStore::Batch` coordinates several
model, scalar-state, and selection operations under one coherent publication;
repeated individual `SetValue` calls still prepare their own candidate pages.
Domain validation sees final outer-batch candidates. C++ field representability
errors, invalid IDs, and unsupported operations can reject during preparation.
Nested batches, rollback, reentrant notification rounds, observer errors, and frame
exclusion follow [STATE.md](STATE.md). Equal final field values suppress canonical
model revisions and notifications.

`ModelChange` owns before/after snapshots, canonical revision, originating model
key, affected fields, and reset/structure flags. Canonical model and registered
selection states publish before any ordinary observer. Changes include other raw
fields affected by a custom record setter. Custom `ModelData::WithEdits` must
preserve schema, identities, and hierarchy; use a structural publication for those
changes.

Built-in column validators run in the final state validation phase. `AddValidator`
adds a model-wide final-candidate rule. `snapshot.WithEdits` is the independent
worker-side operation and validates its completed result before returning it.

```cpp
#include <EStudioUI/Models.h>

#include <stdexcept>

void SetTwoLengths(EStudioUI::ItemModel& model,
                   EStudioUI::ItemId first, EStudioUI::ItemId second)
{
    using namespace EStudioUI;
    ScopedConnection rule(model.AddValidator([first, second](const ModelSnapshot& candidate)
    {
        const double a = std::get<double>(candidate.GetValue(first, 1));
        const double b = std::get<double>(candidate.GetValue(second, 1));
        if (a < 0 || b < a)
            throw std::invalid_argument("Lengths require 0 <= first <= second.");
    }));
    model.GetStore().Batch([&]
    {
        model.SetValue(first, 1, 20.0);
        model.SetValue(second, 1, 30.0);
    });
}
```

`ModelBuilder` builds flat or hierarchical data on any thread. `Restructure`
creates a builder from the current snapshot for insert/remove/move operations.
It performs explicit linear work and retains typed records, unexposed fields,
and custom roles. New records need a default-constructible C++ record and writable
initialization accessors; otherwise prepare a typed replacement snapshot.
External adapters can override `WithStructure` or remain structurally read-only.
No-op structural moves preserve storage identity and avoid a publication.

## Selection and projections

Selection owns current item, current schema column, selected set, and range
anchor. Single selection is the default. `SetMode(SelectionMode::Multiple)` enables
Ctrl-toggle and Shift-range interaction in collection views. `SetItems` replaces
a large selected set in one publication. Selection snapshots use an owned ordered
set; individual selection edits copy the selected set, proportional to selected
count. Bulk selection should use `SetItems` or `SelectRange`.

Sorting preserves identities. Filtering retains hidden selections. Deletion
prunes current, anchor, and selected IDs in the model's transaction. A filtered
list/table reports local index `-1` for a hidden current item while
`GetCurrentItemId()` retains its canonical ID. Tree `GetCurrentItem()` always uses
identity. Scroll and expansion remain local to each view; explicit index/item
reveal operations scroll their receiving view.

`SortFilterModel` sorts raw values numerically and stably within sibling ranges;
filtering retains matching descendants' ancestors. It stores identity mappings
and shared snapshots, without copying records. Sort/filter construction and
relevant updates may scan the complete source; these are explicit measured costs.
A filter's optional column dependency list avoids rebuilding for unrelated field
edits. An empty dependency list conservatively depends on every field. Predicates
should depend on their snapshot argument and owned fixed settings; change the
filter explicitly when its settings change.

Adapter chains prepare and validate derived mappings before upstream publication,
including rule-only changes. A throwing predicate rejects that candidate. Prepared
mappings publish in subsequent coherent state notification rounds; repaint/frame
processing remains excluded until the complete drain finishes. A canonical-model
observer should use its own `ModelChange` snapshot; a projection observer receives
its prepared projected snapshot. All source and derived changes settle before the
outer successful operation returns. Derived observer/allocation failures after a
canonical commit follow the committed-error policy in STATE.md.

## Presentation, editing, and CSS

`ModelColumn` supplies optional formatting, parsing, validation, editability, and
logical-pixel width hints. Raw numeric data stays numeric even when display text
includes units. The default text editor uses raw text. Failed user commits keep
the canonical value and editable input, recording `TableWidget::GetEditError()`
and emitting `EditRejected()`. Programmatic failures propagate to their caller.
The existing `CellChanged` signal reports committed visible fields when connected;
use `ItemModel::Subscribe` for one coherent change object per large batch.
If the underlying cell changes externally during editing, a conflicting commit
is rejected. Sorting follows the edited item's ID; removing/filtering it cancels
the editor. Editor startup prepares geometry and must occur outside a state update;
use the normal deferred UI boundary when starting it from an observer.

`ItemDelegate` customizes display text, semantic style, GPU contents, and parsing.
`SetItemDelegate` works on shared list/tree items and per table column. A delegate
can use `Renderer`/`Painter` drawing through the existing GPU mechanism and return
false from `Paint` to retain the default cached text. Paint callbacks have the
usual traversal mutation restrictions. Ordinary row/cell CSS parts remain cached
per view; records never become child widgets.

| Role | Default behavior |
| --- | --- |
| Edit | Owned raw field value |
| Display | Column-formatted text, or the raw value's default formatting |
| Foreground / Background | Optional color over the CSS part; selection CSS takes precedence |
| Enabled | Defaults true; false dims contents and prevents user activation/editing |
| Checked | Boolean raw fields expose their value; presentation remains delegate-controlled |
| ToolTip | Optional semantic text for applications/future tooltip infrastructure |

Color columns need an appropriate formatter/delegate for useful display and an
explicit parser/delegate for text editing; member-based color columns default to
read-only. String values are owned; views sanitize displayed UTF-8 through the
existing text pipeline.

Shared tables default to bounded sampled content widths. `SetColumnSize` sets a
Fixed/Content/Fill contract without changing the model. `SetContentMeasurePolicy`
selects Hints, Visible, Sample, or Exact. Visible ranges refresh on scrolling;
Sample defaults to the first 64 rows. `FitColumnsToContents()` explicitly scans
all displayed root rows once. Convenience-owned tables retain their existing
exact content-width behavior.

`SetItems`, `SetRows`, and `SetColumns` retain their convenience-owned behavior;
full replacement detaches a shared list/table model. `GetModel()` returns empty
in that mode. Compatible item/row convenience edits on shared models route to
canonical typed edits or explicit structural rebuilding. Display getters now
return owned strings, so callers can keep their results across model updates.

## Workers and external/lazy storage

Capture a snapshot and expected canonical revision on the owner; the worker owns
its data and the completion receives a live model. A changed canonical revision
rejects the stale replacement. RequestScope/RunLatest provides request supersession
independently of data revisions. Closing the store or destroying the model expires
its typed handle, preserving the normal task/receipt outcomes.

```cpp
#include <EStudioUI/BackgroundTasks.h>
#include <EStudioUI/Models.h>

EStudioUI::Task RefreshNames(EStudioUI::BackgroundTasks& tasks,
                            EStudioUI::RequestScope& requests,
                            EStudioUI::ItemModel& model)
{
    using namespace EStudioUI;
    auto snapshot = model.GetSnapshot();
    const auto revision = model.GetRevision();
    return tasks.RunLatest(requests, model.GetHandle(),
        [snapshot](TaskContext& task)
        {
            std::vector<ModelEdit> edits;
            for (std::size_t index = 0; index < snapshot.GetItemCount(); ++index)
            {
                if (index % 1024 == 0)
                    task.ThrowIfCancellationRequested();
                const auto item = snapshot.GetItemByIndex(index);
                edits.push_back({item, 0, snapshot.GetText(item, 0) + " *"});
            }
            return snapshot.WithEdits(edits);
        },
        [revision](ItemModel& live, ModelSnapshot updated)
        {
            live.Replace(std::move(updated), revision);
        });
}
```

Subclass `ModelData` to expose external immutable storage, sparse data, or lazy
value access. Queries must be nonblocking, pure, and safe for concurrent readers.
The framework validates schema/changed fields without scanning every external
record. The provider guarantees valid unique IDs, consistent row/parent/position
queries, acyclic hierarchy, and valid raw values. Optional editing/structural
methods default to clear read-only diagnostics.

The tree's `TreeRowMap` indexes expanded-branch counts/prefixes, requiring work
proportional to expanded identities rather than all flat/collapsed rows. Random
viewport access follows branch prefixes. Sorting/filtering still requires the
relevant complete identity mapping. Built-in asynchronous database/network page
fetch scheduling and cache eviction remain future provider work; their results
can use the owned publication API above. The existing canvas keeps its geometry
storage and BVHs; an application can map canonical IDs to canvas entity IDs.

The Models gallery and [verification/MODELS_0_11.md](verification/MODELS_0_11.md)
record actual 100000-record work counts, editing/lifetime tests, and real retained
versus full framebuffer results. Layout is delivered in 0.12 with the optional
constraint strategy in 0.13. F09 input/actions/focus is delivered in 0.14.
Gate B remains open for F11 platform/accessibility and F10a essential text/input in
[FOUNDATIONS_ROADMAP.md](FOUNDATIONS_ROADMAP.md). F14 module extraction and SDK
packaging are deferred until broad widget/workspace use informs their boundaries.
