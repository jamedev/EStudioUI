# Linear constraint layouts and live resizing / EStudioUI 0.13

`ConstraintLayout` implements the optional linear solver on the F08 measurement
and strategy boundary. Ordinary Box/Grid/Form/Flow layouts remain available.
Open **Layout lab → Constraint layouts** (or the **Constraints** tab on a wide
window). The demo contains working editors, numeric controls,
anchored panels, resize presets, and an aspect-ratio badge.

## The ordinary API

Create children normally, capture their anchors, add relationships, and install
the strategy by value. This is the central part of the complete, compiled
[example](examples/ConstraintLayout.cpp):

```cpp
const ConstraintItem parent = ConstraintItem::Parent(), side(sidebar), field(editor);
ConstraintLayout layout;
layout.Add(side.Left() == parent.Left());
layout.Add(side.Top() == parent.Top());
layout.Add(side.Width() == 200, ConstraintStrength::Strong, "sidebar preference");
layout.Add(field.Left() == side.Right() + 12);
layout.Add(field.Right() == parent.Right());
layout.Add(field.Top() == side.Top());
root.SetLayout(std::move(layout));
```

Include `EStudioUI/ConstraintLayout.h`. The example target is
`EStudioUIConstraintExample`; run from its build directory alongside the demo
assets. The existing one-shot Windows command installs the new dependency and
builds/tests the project as before:

```powershell
.\Build.ps1 -Configuration Release -Run
```

`ConstraintItem(widget)` captures a lifetime-unique identity, without retaining
or dereferencing the widget later. Anchors must refer to displayed, in-flow
immediate children of this container. Hidden, removed, out-of-flow, or foreign
anchors deactivate their rules and produce named `ConstraintInactive`
diagnostics. Showing a child restores its rules; a replacement child cannot
inherit a deleted child's anchors. Use another constraint container to express
relationships inside a nested composition.

| Expression | Meaning |
| --- | --- |
| `Left`, `Right`, `Top`, `Bottom` | Outer border edges, excluding margins |
| `Width`, `Height` | Outer dimensions, including border/padding |
| `CenterX`, `CenterY` | Outer rectangle centers |
| `ConstraintItem::Parent()` | Inner content rectangle; left/top are zero |
| `a.Width() == b.Width()` | Equal sizes |
| `a.Width() <= 320` | Upper bound |
| `a.Height() == a.Width() / 2` | Linear 2:1 aspect ratio |

Expressions support addition, subtraction, scalar multiplication and division.
Products of variables, trigonometry, and nonlinear sketch constraints are outside
this solver. Baseline alignment remains available through Form/Box/Grid layouts;
height-dependent painted baseline offsets are not linear anchors here. CSS
styles ordinary controls exactly as in other containers. Relationships are C++
objects, not new CSS syntax. Configure before `SetLayout`; an installed strategy
is immutable through `GetLayout`. Copy/replace it at an idle owner boundary to
change relationships.

## Size contracts, priorities, and conflicts

| Rule | Precedence and behavior |
| --- | --- |
| Widget minima/maxima, Fixed and resolved CSS pixel/percentage dimensions | Hard internal bounds, installed before application constraints |
| `Required` | Hard relationship; incompatible later rules are omitted and named, preserving hard widget limits and the accepted earlier prefix |
| `Strong`, `Medium`, `Weak` | Kiwi weights 1,000,000 / 1,000 / 1; minimize weighted absolute normalized error |
| Content dimension | Preferred measured size, weight 1, within hard limits |
| Fill dimension | Preference for available dimension minus margins, weight 0.1, within hard limits |
| Containment | Soft margin-aware bounds, weight 10; insufficient space can overflow |
| Unspecified position | Weak preference for the leading margin, weight 0.0001 |

Soft strengths are weighted objectives, **not strict lexicographic priorities**:
sufficiently many weaker errors can outweigh a stronger preference. Equal-cost
solutions have no application-defined tie-break; add a rule when the choice
matters. The strategy's Fill preference does not automatically divide space by
weight as Box/Grid do: specify equal widths, ratios, or spacing explicitly.
`SetGap` and alignment/justification do not synthesize relationships. Margins
affect default containment/preferences; explicit anchors use border edges.

A viewport smaller than hard child requirements does not silently shrink a
Fixed/minimum size. The result remains finite and renderable, with
`ConstraintConflict` for omitted required relationships and `Overflow` for
children beyond the container. This deterministic recovery is a visible error
policy, not a claim that an infeasible system was satisfied. A failed solver
addition rebuilds the accepted prefix so partially modified solver state cannot
escape. A later feasible resize retries previously rejected rules.

Constraints can intentionally overlap or extend outside their parent; the
container's implicit aggregate minimum is zero. Set an explicit minimum with
`SizeRule::Fill/Content` when required. Child hard limits remain enforced. Natural
container size minimizes its extent around preferred content; explicit viewport
allocation is fixed. Standard ancestor clipping still applies.

## Width-dependent content without feedback loops

The engine solves the linear system using natural child measurements first.
When a child has height-for-width behavior, it measures that child at the solved
width, freezes horizontal geometry, and solves the vertical result once. No
iteration, content/percentage feedback, or convergence guessing occurs. A
height-dependent measurement must preserve its horizontal minimum. An aspect
ratio or other cross-axis relationship that becomes infeasible after wrapping
is reported through the same required-rule conflict policy; it does not drive
width back through a fixed-point loop. Without height-dependent children, a
single solve suffices.

The existing independent-percentage-reference policy remains. A definite
container allocation or pixel-sized child supplies an independent reference.
A content-derived container size does not gain definiteness merely because it
has a numeric solution; unresolved percentages use Content with diagnostics.
This first version conservatively does not infer percentage definiteness from
an arbitrary graph of user equations. Use an explicit Fixed size or independently
allocated container when descendants need such a reference.

## Work and diagnostics

The only new dependency is private, header-only Kiwi 1.4.8, installed through the
SHA-512-pinned project vcpkg overlay. It adds no binary/transitive dependencies.
Its BSD and bundled permissive container notices allow commercial use; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

At most three query variants retain a pair of solver tableaux, alongside F08's
three-entry widget measurement caches. Feasible numeric resizes update edit variables while retaining unchanged
constraints. Hard viewport/frozen-width targets are speculative edit suggestions
only inside the private fast path: every target is checked within 0.001 logical
pixels before exposing a plan. Rejected rules or displaced targets trigger an
exact rebuild with actual Required rows. An axis-independent vertical pass can
reuse horizontal results without extra pins, after checking they remain the same.
Ordinary changed hard bounds remove/replace their rows. Topology changes and
infeasibility rebuild. Identical row sets reuse solved values. Storage scales
with active widgets/rules and tableau density, not resize history. There are
limits of 10,000 rules and 10,000 terms per rule; these are validation ceilings,
not a performance claim. Coefficients/constants must be finite within +/-1e12;
equivalent scalar multiples are normalized before solving. Solutions beyond
1e9 logical pixels are rejected. Ill-conditioned systems still require sensible
application units. This is for UI containers; keep large canvas item populations
in the canvas spatial/data architecture.

`GetLayoutReport()` owns child rectangles, named expressions, priorities,
statuses, normalized residuals, and issues; `Explain()` formats them only on
request. Expression descriptions compile when rules are added, not on resize.
`UiContext::GetLayoutDiagnostics()` aggregates visible container issues.
`ConstraintLayout::GetStats()` exposes builds, incremental updates, actual
solves, and unchanged-row cache hits. Paint-only style changes perform no solve
or arrangement; settled UI rendering submits zero GPU draws.

## Resizing while the mouse button is held

The previous callbacks prepared/requested UI updates but waited for the outer
application loop to paint. Win32 can run a system modal loop inside native event
processing while a border/title bar/menu is held, preventing that loop returning.
This is separate from solver performance.

Window size and refresh callbacks now service safe pending frames directly.
On Windows, a private HWND subclass starts a timer on `WM_ENTERSIZEMOVE` /
`WM_ENTERMENULOOP` and stops it on exit. The timer services registered windows,
bounded UI queue delivery, animation/caret clocks, layout, scene preparation,
painting and buffer swap without recursively polling native events. Windows
continue progressing even if the cursor pauses while the border remains held.
Native handles stay inside the platform implementation. Closing the owner
cancels its modal operation; destruction detaches its subclass and timer.

These frames use the same `FramePacer` as ordinary presentation, including the
default 60 Hz cap, vsync, visibility/minimize/focus policy, and no catch-up burst.
Callbacks inside an existing frame, queue service, nested native action, or
state batch defer until the next safe boundary. Contexts restore after native
callbacks; errors surface at the C++ scheduler boundary. Resize invalidation
preserves CSS and intrinsic caches; actual viewport and framebuffer dimensions
are queried before each frame, including DPI changes.

This removes the wait-until-release behavior from the implementation. It does
not guarantee 60 actual frames/second on arbitrary hardware, workloads or OS
scheduling. Win32 timers are approximate with a 10 ms floor; resize/damage events
also tick immediately. Application callbacks still run on the owner and must
remain short. Native render-thread decoupling is not introduced. Actual
cross-monitor DPI, driver behavior, compositor pacing and human border-drag
smoothness still need hardware validation. See
[verification/CONSTRAINTS_0_13.md](verification/CONSTRAINTS_0_13.md) for the
separate CPU, framebuffer, sanitizer and Win32 modal-loop evidence.
