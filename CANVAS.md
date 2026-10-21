# Infinite canvas and sketcher / EStudioUI 0.7

`CanvasScene` stores lightweight geometry in contiguous memory, with stable
scene-local generational IDs and a balanced dynamic AABB BVH. `CanvasWidget`
projects only viewport candidates into the existing OpenGL renderer. It does not
create one `Widget`, signal object, or CSS selector node per scene item.

The new **Sketcher** demo tab is a working two-dimensional geometry editor.
Run the existing Windows dependency/build/test/launch command:

```powershell
.\Build.ps1 -Configuration Release -Run
```

## Editing

- Select (V): click an outline, drag selected geometry, or drag empty space to
  select fully enclosed shapes. Shift adds/toggles selection.
- Line (L), Rectangle (R), Circle (C): drag to create. Circle drag sets center and
  radius. Shift constrains a new line horizontally or vertically.
- Arc (A): click center, start, and end; the signed sweep is also numerically editable.
- Spline (B): click four cubic Bézier control points. Path (W): click vertices,
  then Enter or double-click to finish; the inspector can close the path.
- Text (T): click to insert the configured annotation. Its explicit bounds and
  font size in world units can be edited in the inspector.
- The precision inspector creates geometry from numbers and applies edits to a
  selected item. Spline/path vertices are editable through an index and X/Y fields.
  Set size changes line length or circle/arc radius; Horizontal/Vertical directly
  align line endpoints. Group, Ungroup, and Duplicate are undoable commands.
- Pan (P), Alt+drag, or Space+drag: move the view. Wheel zooms around the pointer.
- Snapping uses nearby endpoints, midpoints, circle centers/cardinal points,
  then a ten-unit grid. Disable the Snap toggle for free placement.
- Delete removes selection. Ctrl+Z and Ctrl+Y undo/redo; Escape cancels previews.
- Fit geometry frames the scene. Reset sample loads the editable plate example.
- The world-anchored text editor and button are ordinary CSS-styled widgets.
- Save/Open use the editable file path and the versioned `.esketch` format.
  Loading is transactional and clears selection/history after a successful load.

Undo keeps at most 128 commands and 128 MiB of command payload, evicting the
oldest commands when needed. A group move/delete that cannot fit as one command
returns false before modifying geometry or history. The 200,000-item group
move/delete/undo path is covered by automated tests.

This sketcher edits geometric primitives and groups. It does not yet solve dimensional,
coincident, tangent, or other parametric constraints, nor import/export DXF/STEP.
The spatial index is independent of any future constraint solver.

## Scene and view

```cpp
auto& canvas = parent.Add<EStudioUI::CanvasWidget>();
canvas.SetHeight(EStudioUI::SizeRule::Fill());
const auto style = canvas.RegisterItemStyle("canvas-item", {"construction"});
canvas.GetScene().Add({EStudioUI::CanvasPrimitive::Line,
                      {0, 0}, {100, 50}, 0}, style);
canvas.SceneChanged();
```

Model coordinates, camera centers, picking, and snapping use doubles. Conversion
to GPU floats happens after subtracting the camera, preserving small local offsets
at large world coordinates. Coordinates use x-right/y-down. Zoom is clamped to
1e-6 through 1e6 logical pixels per world unit. “Infinite” means no fixed document
rectangle; IEEE floating-point range and precision still apply.

Direct scene mutation requires `SceneChanged()` on every affected view to request
a frame. The scene revision also invalidates each view's cached query. IDs remain
stable through unrelated dense-storage moves, but removal/clear invalidates them.
Pointers returned by `Get` and `GetItems` do not survive structural mutations.
All mutation, widget, rendering, and query-statistic APIs are GUI-thread APIs.

`Query` is a broad-phase inclusive bounds query, including zero-width lines.
`SketchDocument::Pick` follows it with analytic outline distance, and selection
boxes require full containment. Painter order remains stable across edits and
undo. The model stores geometry, style handle, selection flag, and order together;
it is data-oriented, but it is not a general-purpose ECS framework. Future component
pools can be added without replacing the existing interactive widget hierarchy.

## Shared CSS palette

```cpp
std::string error;
canvas.SetItemStyleSheet(R"css(
  canvas-item { background: rgba(20, 100, 130, 0.15);
                color: #66ddcc; border: 2px solid #66ddcc; }
  canvas-item.construction { color: #8796b0; }
  canvas-item:selected { color: #ffcc66; border-color: #ffcc66; }
)css", error);
```

The item stylesheet is separate from the window stylesheet. One palette entry
holds a selector identity and precomputed normal/selected styles. CSS is parsed
and selectors are resolved on registration/reload, never per item during painting
or per animation tick. Unknown palette indices fall back to entry zero.

Lines, arcs, splines, and paths use `color`, `border-width`, and opacity; widths stay in logical screen pixels
while geometry zooms. Rectangles/circles use background, border, radius (rectangles),
gradients, shadows, and opacity. The normal/selected palette contributes a
conservative stroke/shadow margin to viewport queries. Dense items do not run
CSS transforms, layout rules, pseudo-elements, or automatic CSS animation timelines.
`display` is a layout declaration and is ignored for dense items; removing an
item from the scene controls its presence in both drawing and spatial queries.
Normal hosted widgets retain the full widget CSS/animation/input behavior.

`HostWidget` owns a sparse regular widget at a world rectangle. Its allocated
rectangle scales with the view; text and border metrics remain normal logical-pixel
CSS sizes. It is clipped by the canvas and uses normal focus/capture/lifetime
rules. Hosting hundreds of thousands of full widget trees is not the dense-item
path. Hosts are an overlay above scene geometry and are not indexed by the scene
BVH or serialized as geometric sketch primitives.

## Rendering and measurement

A line is one analytically antialiased capsule instance with a tight oriented
quad. Circles reuse the analytic rounded-box shader. Visible IDs are sorted in
painter order and carry stable item identities into retained damage tracking.
Static canvases request no continuous frames. Offscreen edits can request a CPU
frame while producing zero analytic redraw, and unchanged frames retain pixels.

The 200,000-item demo keeps a local view so culling is observable. This measures
large scene storage and viewport queries, not the cost of drawing every object at
once. Query work depends on overlap/distribution; painting and sorting scale with
visible candidates. Fully overlapping scenes or Fit geometry on the entire stress
scene can expose all items. Hierarchical LOD, density aggregation, cached tiles,
and GPU-driven indirect submission are future work. Circles still rasterize their
bounding square; drawing many overlapping translucent shapes has a real cost.

```powershell
.\build\windows-vcpkg\Release\EStudioUIDemo.exe --profile-canvas --output benchmark-local\canvas
```

This records insertion/BVH build time, 100 isolated CPU queries, and 32 completed
800x600 pan frames after four warmups, for 20k and 200k items at equal local density.
The CSV includes candidate count, visited BVH nodes, shape instances, draws, and
repaint pixels. Completed-frame time includes CPU and synchronous completion; it
is not a GPU-only timer or a Windows utilization prediction. Linux OSMesa can use
`--headless --profile-canvas`. Record the driver and compare on your target GPU.

Automated checks cover BVH queries against brute force after random edits,
generational identity reuse, undo/redo, persistence, large-coordinate transforms,
widget hosting, input gestures, 200k viewport submission, and real framebuffer
comparison between retained and full redraw. No new runtime dependency is added.

## Curves, annotations, and groups

```cpp
using namespace EStudioUI;
auto arc = MakeCanvasArc({0, 0}, 50, 0, 1.5707963267948966);
auto spline = MakeCanvasSpline({100, 0}, {{0, 0}, {20, -40}, {60, 40}, {80, 0}});
auto path = MakeCanvasPath({0, 100}, {{0, 0}, {50, 0}, {30, 40}}, true);
auto text = MakeCanvasText({10, 10, 160, 30}, "Plate A", 18);
auto group = MakeCanvasGroup({200, 100}, {arc, spline, path, text});
auto id = document.AddShape(group);
// Document edits are undoable; direct CanvasScene edits intentionally bypass history.
document.UpdateShape(id, TranslateGeometry(group, {10, 0}));
```

Angles are radians, clockwise in the y-down world; sweeps can be negative and
cannot exceed one full turn. Indexed bounds conservatively include arc centers and spline control handles so
snapping can discover them. Spline controls are local to the supplied origin
and contain 3n+1 points for a connected cubic chain. Paths are stroked polylines,
with an optional closing segment. Text is single-line UTF-8 and clipped to its
explicit world rectangle; its world-size glyphs zoom with the canvas.

A group owns local child geometry and optionally a matching vector of child
palette indices. Without explicit child indices, children inherit the group's
style. Selection styling propagates through the group. Groups have no paint layer; a
leaf applies its complete style exactly once, so grouping does not multiply
opacity. `MakeCanvasGroup` builds
an immutable child BVH, so viewport queries and picking can descend only into
nearby children. Editing a child creates a replacement group; translating a
group reuses its payload/index. Group bounds and normal translations are O(1).

Complex geometry is an immutable shared value. Use the `MakeCanvas*` factories;
never mutate a payload through a retained non-const alias after publication.
The model avoids a deep copy on every insertion. Raw payloads are validated and
uncached groups fall back to recursive queries. The factories validate nesting,
coordinates, text, and payload size before publication. Maximum group depth is
32; curve payloads are limited to 65,536 points and annotations to 65,536 bytes.

Adaptive curve flattening targets 0.35 logical pixels of error using zoom
buckets. The cache uses local coordinates, preserving reuse across panning and
movement. Curve/text caches retain at most 4,096 entries and an estimated 16 MiB
combined; `GetCacheStats()` reports hits, misses, retained bytes, and quality
budget exhaustion. Extremely complex curves have a bounded tessellation budget;
its exhaustion can reduce geometric accuracy rather than allocate indefinitely.
Strokes are batched analytic round-capped segments; translucent joins can overlap.
General filled paths, dash patterns, and CAD-grade offset/stroke joins are not
implemented. Annotations use cached shaped text and analytic GPU glyphs.

## Document reuse and persistence

`SketchDocument` is independent of OpenGL and windowing. `UpdateShape`,
`DuplicateSelection`, `GroupSelection`, `UngroupSelection`, `SetHorizontal`,
`SetLineLength`, and `SetCircleRadius` create bounded undoable commands. The
geometry setters are direct edits, not persistent dimensional constraints.
`SketchWidget` owns interaction previews and emits `Changed()` after its edits;
when editing its document directly, call `SceneChanged()` and update application
inspectors as appropriate. Selected group children are edited as a group or after
ungrouping.

Save writes `.esketch` version 2 with recursive geometry and child palette IDs;
Load accepts both version 1 and version 2. Loading is transactional, with finite
coordinate validation and aggregate caps of one million geometry nodes/control
points and 16 MiB of annotation text. Stylesheet text, camera state, hosted widgets,
selection, and command history are application state and are not serialized.
Style IDs refer to the application's registered palette. Undo storage counts
complex payloads and group indices conservatively against the existing 128 MiB
budget.
