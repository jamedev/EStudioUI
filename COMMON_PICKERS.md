# Common controls (0.26)

The **Common controls** gallery demonstrates these ordinary C++ controls. All use
the existing GPU renderer, semantic tree, input/capture boundary and theme
cascade. Creator can instantiate them; specialized complete property adapters
remain an explicit follow-up to its existing general geometry/style support.

- `SegmentedControl::SetItems` creates up to 64 exclusive segments, with normal
  `RadioGroup` semantics, arrow navigation and focus. `SetCurrentIndex` and
  `CurrentChanged` expose the selected segment.
- `MultiSelectComboBox` takes either bounded convenience captions or a shared
  `ItemModel` column. Its virtual list shares canonical `SelectionModel` state.
  Pointer clicks and Space toggle choices; Escape/outside click closes. Selected
  identity count is bounded to 128. `GetSelectionModel` enables application-driven
  bulk selection. The visible summary displays up to three captions plus a count.
- `TagEditor` holds up to 128 unique nonempty tags of at most 256 bytes. Enter
  adds free-form values, removable chips preserve order, `SetEditable(false)`
  hides free-form entry and `SetSuggestions` supplies an optional multi-choice
  catalog. `SetTags` publishes one complete owning value; `BindTags` shares it
  with a validated `State<vector<string>>`.
- `RangeSlider` draws one track, an interval and two GPU handles. `SetRange`,
  `SetStep` and atomic `SetValues(RangeValues)` define its domain. Handles clamp
  at each other. Tab visits both endpoints, arrows/Page/Home/End change the
  active endpoint, and captured dragging works outside the thumb. Each endpoint
  has a stable virtual Slider accessibility identity. `BindValue` rejects
  nonfinite, inverted or out-of-range final state candidates before commit.
- `SelectableLabel` reuses the complete read-only, single-line `TextBox` engine,
  including grapheme/bidirectional selection, copy and native text accessibility.
  This deliberately does not introduce a second text editing engine. Multiline
  editable documents remain the F10b track. Ordinary `Label` still supports
  wrapping display text.
- `LinkLabel` adds a target and `Activated(string)` to selectable text. A drag
  selects; a click or Enter activates. The application decides how to route the
  target, including internal commands or external navigation. The widget does
  not execute a shell command or launch an OS application implicitly.
- `FontEditor` edits `FontSelection` family, logical pixel size, weight and style,
  with a cached live preview. `RefreshFamilies` enumerates fonts registered in
  the shared `FontFace`; it does not enumerate or silently load every OS font.
  Size is 1–512 px and weight 1–1000. Direction/language remain part of the
  complete retained selection. `FontPicker` edits a copy in an owned popup and
  commits only on Apply. Cancel/outside dismissal retains the applied value.
  `BindFont` supports both forms.
- `Calendar` paints bounded month cells without creating a widget per day.
  Previous/next, arrow/week/month keyboard navigation and virtual semantic dates
  use Gregorian rules over years 1–9999. `Activated` reports an accepted date.
- `DateEdit` combines an ISO text field and an owned calendar. `TimeEdit` combines
  validated local clock input, Up/Down second stepping and an Apply/Cancel picker.
  `DateTimeEdit` composes both and publishes complete optional pairs. Unfinished
  drafts remain separate from committed values. `Commit()` rejects invalid drafts
  without substituting an unrelated value. Programmatic setters validate before
  changing their control.

`CalendarDate`, `ClockTime` and `DateTimeValue` provide pure, locale-independent
parse/format/value APIs. Dates use `YYYY-MM-DD`; clock input accepts `HH:MM`,
`HH:MM:SS`, or `HH:MM:SS.fff`. Local datetime uses `T` between components. Empty
edit fields represent an optional empty value. Timezones, daylight-saving
conversion, localized calendars, natural-language dates and leap seconds are
outside this civil-value contract.

```cpp
State<RangeValues> interval(ui.States(), {20, 80});
auto& slider = parent.Add<RangeSlider>();
auto intervalBinding = BindValue(slider, interval);

auto& date = parent.Add<DateEdit>();
date.SetDate(CalendarDate{2028, 2, 29});

auto& font = parent.Add<FontPicker>();
State<FontSelection> typography(ui.States(), FontSelection{});
auto fontBinding = BindFont(font, typography);
```

`BindDate`, `BindTime`, `BindValue(DateTimeEdit, ...)`, `BindText(SearchBox, ...)`
and the bindings above reuse existing final-candidate validation, scoped receiver
expiry and rollback. Retain a `ScopedBinding` for scope-based disconnection.

CSS parts are `rangeslider::groove`, `::fill`, `::lower-handle`, `::upper-handle`,
`calendar::day` and `::header`. Existing selector/effect support applies to every
composed editor, segment, tag and popup. `textbox.selectable-label` and
`textbox.link-label` style read-only labels. Every predefined theme supplies light
and dark defaults; no per-frame stylesheet parsing or new runtime dependency is
introduced.

See the [verification record](verification/PRODUCTIVITY_0_26.md) for exact tested source and platform evidence.
