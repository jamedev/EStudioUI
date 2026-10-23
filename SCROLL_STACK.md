# Standalone scrollbars and page stacks (0.21)

The **Scrolling & pages** demo combines a lightweight drawing viewport with
persistent properties and note pages. The [compiled example](examples/ScrollStack.cpp)
uses the ordinary public API. These controls add no runtime dependency.

## ScrollBar

```cpp
auto& bar = parent.Add<EStudioUI::ScrollBar>(EStudioUI::Axis::Horizontal);
bar.SetRange(0, 900);       // Allowed viewport origins.
bar.SetPageStep(100);      // Visible extent out of 1,000 content units.
bar.SetSingleStep(5);
bar.ValueChanged().Connect([](double offset) { /* Update the viewport origin. */ });
```

The default axis is vertical, range is 0..100, page extent is 10 and single step
is 1. Values increase rightward/downward. Range limits and values must be finite;
limits may be equal. Programmatic values clamp without quantization. A zero
range is immovable and fills an automatically sized thumb; a zero page extent
is allowed and retains the CSS minimum thumb size.

The thumb's natural length is `track * page / (maximum - minimum + page)`.
For a pixel-based viewport, the owner supplies `maximum = max(0, content - viewport)`
and `page = viewport`, refreshing both when its content or viewport changes.
ScrollBar does not own, measure or virtualize that content. ScrollArea remains
the ready-made container for ordinary widget children; collections retain their
own viewport-based row painting. The gallery uses a fixed logical viewport whose
presentation scales with the available pixels.

Thumb dragging retains the captured pointer and button through the shared input
boundary. A track click moves one page towards the pointer. This increment has
no hold-to-repeat timer or arrow end buttons. Axis arrows move one single step;
PageUp/PageDown move one page, Home/End select endpoints. Wheel notches use three
single steps and unchanged movement at an endpoint bubbles to the parent.
Deactivation and capture loss cancel dragging. Root transforms and ancestor
clipping use the same input geometry as existing controls.

The standalone control and ScrollWidget collections call the same proportional
thumb, track-inset and normalized drag arithmetic. Collections preserve their
existing track-click jump behavior. Extreme ordered finite double ranges remain
finite, including a span that would overflow double subtraction.

`ValueChanged` publishes real canonical changes after committing state. Reentrant
changes settle iteratively with a 64-turn bound; an observer may remove the control.
`BindValue(bar, state)` supports the existing one-way/two-way State validation,
rollback and lifetime rules. Direct access is owner-only; workers use State or
typed widget handles. Accessibility exposes a ScrollBar range and canonical
SetNumber action, mapped to the Windows UIA scrollbar control type and existing
RangeValue pattern.

## StackedWidget

```cpp
auto& pages = parent.Add<EStudioUI::StackedWidget>();
pages.SetHeight(EStudioUI::SizeRule::Fill());
auto& general = pages.AddPage();
general.Add<EStudioUI::TextBox>("Name");
auto& advanced = pages.AddPage();
advanced.Add<EStudioUI::TextBox>("Advanced setting");
pages.SetCurrentPage(advanced);
```

The first page becomes current automatically; an empty stack has index -1 and
null GetCurrentPage. AddPage creates an ordinary fill-sized container. Use
`AddPage<MyWidget>(constructorArguments...)` or adopt a unique_ptr for a custom
page; its explicit size contracts are preserved. Every page receives the CSS
class `stack-page`, including custom widget types.

GetPage/GetPageCount, SetCurrentIndex/SetCurrentPage and GetCurrentIndex expose
page selection without a visible tab strip. RemovePage destroys that page and
preserves the selected identity when removing an earlier page; removing the
selected page chooses its next neighbour, or its predecessor at the end.
ClearPages publishes one empty selection. Invalid indices and unrelated pages
throw before changing selection. Use these page APIs for ownership/visibility;
raw inherited child mutation is hidden on the concrete StackedWidget API.

CurrentChanged observes committed ownership, index and page visibility. It also
reports a replacement page at the same numeric index. Reentrant selection is
iterative and bounded to 64 notification turns. Observers may remove pages or
the stack. If first-page notification removes the newly adopted page, AddPage
throws rather than returning a dangling reference.

Inactive pages retain their child state but are excluded from intrinsic layout,
input, painting and continuous animation requests. Shared input preparation
retires hidden focus, pointer capture, popups and composition; switching does
not commit preedit or steal focus for the new page. Use normal focus APIs when
an application wants a particular editor focused. Pages are semantic groups,
without advertising nonexistent tab-list selection commands. Selection is a
small UI-owned page set; switching/visibility updates are linear in page count.
This is not a virtual document/page model.

## CSS and retained work

`scrollbar::scroll-track` and `scrollbar::scroll-thumb` use the existing collection
part names, including `:horizontal`, `:vertical`, `:hover`, `:pressed`, `:focus`
and `:disabled` state where applicable. Track thickness determines intrinsic
cross-axis size; padding/border insets the thumb travel, and thumb minimum/maximum
lengths are respected. CSS may set a fixed thumb length instead of the natural
proportion. Hover geometry resolves from the neutral thumb once, avoiding cyclic
hover/measurement feedback. Root and part fills, borders, gradients, shadows,
opacity and decorative transforms use the existing compiled GPU style pipeline.

Style `stackedwidget`, `stackedwidget > .stack-page` and descendants with ordinary
selectors. Explicit page Fixed/Content/Fill rules retain precedence over themes;
only the displayed page supplies layout demand and width-dependent wrapping.
The active default page fills the stack's content allocation on every resize.

Value/page extent changes request paint and retain CSS, intrinsic and arrangement
caches. No new idle animation timer is registered. The C++ framebuffer probe checks
actual hover/pressed pixels, page switches, narrow/wide layouts, exact full-versus-
retained output and zero settled repaint work. See
[verification](verification/SCROLL_STACK_0_21.md) for measured results and limits.
