# Essential controls (0.18)

The **Essential widgets** tab demonstrates the first common-control batch. It provides CheckBox, RadioButton /
RadioGroup, ComboBox, ScrollArea, Splitter, GroupBox, Frame, Separator,
ProgressBar, BusyIndicator and ToolTip. No new runtime dependency is added.
See the compiled [small example](examples/CommonControls.cpp), the interactive
[gallery](demo/EssentialGallery.cpp), and [verification](verification/WIDGETS_0_18.md).

Containers own their children. Direct mutations run on the owner at an idle/input
boundary; worker updates use existing typed handles, State/Bindings or model
publication. These controls share F09 input/actions/popup sessions and F10a text,
fonts, validation and composition. Subclassing and custom painting remain optional.

```cpp
auto& options = window.Ui().Root().Add<EStudioUI::GroupBox>("Sketch settings");
options.SetCheckable(true);
auto& snap = options.Content().Add<EStudioUI::CheckBox>("Enable snapping");
snap.SetChecked(true);
snap.SetToolTip("Snap points while editing geometry.");
auto& units = options.Content().Add<EStudioUI::ComboBox>();
units.SetItems({"Millimeters", "Meters", "Inches"});
units.SetEditable(true);
```

CheckBox has explicit Unchecked, Checked and Indeterminate states. `SetTristate`
controls the user activation cycle; programmatic state remains explicit.
Pointer release, Space/Enter and native toggle actions use one path. `StateChanged`
reports complete states, `Toggled` reports boolean checked changes, and
`BindChecked` connects ordinary `State<bool>` values (indeterminate maps to false).
Boolean checkable actions can drive two-state checkboxes; a presentation becomes
disabled if its action stops being checkable. Observer loops are bounded and
observers can delete their target.

Radio buttons share an explicit `std::shared_ptr<RadioGroup>`. The group holds weak
members, so it can outlive a page. Selection commits all member states before
notifications. Arrow keys select/focus eligible members; required selection can
be made optional. An empty initial group is allowed. Removal expires membership;
it does not invent a replacement selection. Independent toggle actions are
rejected for radio buttons. UI Automation exposes radio selection actions; an
explicit nonvisual group has no separate native container provider.

ComboBox supports convenience captions or an attached ItemModel / SelectionModel.
Its current item is canonical data; popup arrow navigation previews independently,
Enter or one pointer click accepts, and Escape/outside dismissal preserves the
committed choice. Read-only body clicks, Space, F4 and Alt+Down open the popup.
Closed read-only Up/Down/Home/End and Unicode case-folded type-ahead select choices.
Disabled choices are skipped. The arrow is analytic GPU geometry and introduces
no font requirement or extra Tab stop.

Editable mode uses its composed TextBox, available through `GetEditor()` for
validators, selection and native composition. Typed values clear selection while
retaining committed input; they do not insert records into the source model.
`FindCompletion` exposes the default prefix search, and `SetCompletionProvider`
allows owner-side matching against stable item IDs. Completion can highlight an
open popup without rewriting preedit. Reusable asynchronous completion and a
search-box family remain W08.

The popup is one root-owned ListWidget with logical CSS ancestry, shared model
storage and independent preview selection. It paints viewport rows; it does not
create one widget per choice. Type-ahead uses a bounded sorted index, not a full
scan per keystroke/frame. Cold setup/revisions validate text and rebuild the index
with O(n log n) work, including candidate prevalidation before publication.
Limits are 1,000,000 top-level records subject to a 32 MiB prefix-index budget,
plus at most about 4 MiB of numeric navigation positions. Each caption/search
input is bounded to 1 MiB. Model-wide revisions can be expensive; selection and
paint-only changes reuse the index. Existing collection caption caches remain
bounded. SetModel requires an attached idle control; detached convenience items
are supported.

ScrollArea owns a clipping viewport and an ordinary `Content()` widget. Add forms,
layouts and controls there; use List/Table/Tree/Canvas for large virtual datasets.
It reuses wheel, drag and CSS scrollbar behavior. Thumb/track presses are handled
directly by ScrollWidget; release/deactivation terminates capture. Focus movement
reveals nested controls, including transformed geometry. Scrolling invalidates
the dependent viewport arrangement and preserves intrinsic measurement caches. Both axes
are supported; the gallery hides the horizontal gutter through CSS. Arbitrary
child controls retain ordinary widget/layout cost; ScrollArea does not virtualize
a large application widget tree.

Splitter panes are ordinary direct children. `SetSizes({1,2})` assigns positive
stretch weights to every owned pane, including hidden panes; weights divide space
above hard minima through the existing bounded allocator. `GetSizes()` returns
allocated dimensions, with stored positive weights for currently hidden panes.
Weights follow lifetime identities rather than visible child indices. Dragging
converts requested dimensions back into allocator weights; explicit fixed panes
and hard min/max bounds remain authoritative. Arrow keys move the current handle,
Shift uses one logical pixel, and Home/End select the first/last handle. Nested
splitters work normally. Zero-sized/collapsible panes and serialized workspace
schemas are later extensions. Handle paint transforms are clipped to their
reserved gap, with matching inverse-transform hit testing.

GroupBox provides a title and `Content()` container. Its optional checkable header
stays enabled when its content is unchecked. Frame is an ordinary styled
container; Separator supplies horizontal/vertical structure. Configure content
layouts with the existing Box/Grid/Form/Flow/Constraint APIs. Composite controls
own their internal children; applications add content through the documented
container/editor entry points.

ProgressBar uses a finite increasing range, clamps values and supports an optional
percentage caption. Value changes repaint without measuring unrelated controls.
BusyIndicator is a compact unknown-progress bar sharing the same API. Running
visible indicators request the normal capped frames; hidden, clipped, stopped or
disabled indicators stop requesting continuous rendering. The gallery starts
with busy motion stopped. Progress observers have bounded reentrant delivery.

`Widget::SetToolTip(text, delay)` supplies pointer/focus help, bounded to 4,096 UTF-8
bytes and a 0..60 second delay. The window scheduler wakes at the deadline; static
help does not poll at 60 FPS. Help uses a nonactivating, input-transparent owned
popup, including inside a modal presentation. Escape dismisses that top layer
without closing its parent. Other input dismisses help and continues normally.
Composition cancels pending help. Focus/owner loss, expiry and deactivation retire
the presentation; one visible interval is bounded to ten seconds.
Pointer movement resolves help from the actual hovered control and its logical
ancestors. It dismisses unrelated keyboard help instead of borrowing the focused
control's tooltip. Explicit keyboard focus still anchors help beside its source.

The new PopupOptions flags `m_TakeFocus` and `m_InputTransparent` also support custom
noninteractive overlays. Defaults preserve ordinary popup/modal behavior. Custom
composition strategies can call protected `InvalidateChild` when an external
strategy input changes. Protected `GetPreparedLayoutReport` avoids copying reports
inside paint; its borrowed result expires at the next arrangement. Ordinary
application diagnostics continue to use owned `GetLayoutReport()` snapshots.

Rich styling uses [CSS.md](CSS.md) and [essential-widgets.css](assets/essential-widgets.css).
Root boxes and box-shaped parts share gradients, borders, radii, shadows, opacity
and transforms; the existing before/after layers, transitions and keyframes remain
available through the shared engine. No CSS parsing is added to painting.

| Family | Styling hooks |
| --- | --- |
| CheckBox / RadioButton | `::indicator`, `::mark`, `::label`; `:checked`, `:indeterminate`, hover/focus/pressed/disabled |
| ComboBox | Root `:expanded`, `:read-only`, `:empty`, `:focus-within`; `> textbox.combo-editor`, `> toolbutton.combo-arrow`; logical descendant `listwidget.combo-popup` and existing item/scroll parts |
| ScrollArea | Existing `::scroll-track` / `::scroll-thumb`, horizontal/vertical/hover/pressed; `scroll-viewport`, `scroll-content` |
| Splitter | `::handle`, horizontal/vertical/hover/pressed; selected identifies the keyboard handle |
| GroupBox / Frame / Separator | Root styling; `group-title`, `checkbox.group-title`, `group-content`; horizontal/vertical separator states |
| Progress / BusyIndicator | `::track`, `::chunk`, `::label`, owner/part indeterminate state |
| ToolTip | `tooltip` with inherited typography and wrapping |

Checkbox marks use analytic line color/stroke width; radio dots use box paint.
These are short single-line choices, with display text supplied by the registered
font set. Shared image/icon resources follow in [0.20](IMAGE_RESOURCES.md), including
the inherited image-icon slot on checkbox/radio choices. Full mirrored UI layouts,
document editing and other roadmap families remain separate work. Native human IME/assistive-technology/
mixed-monitor acceptance remains tracked in the platform checklist.

The 0.26 [remaining common controls](COMMON_PICKERS.md) add segmented and multiple choices, tags, intervals, selectable/link labels, fonts and civil date/time pickers.
