# Themes and Creator authoring follow-up

Date: 2026-10-24. This follows the 0.23 property/Creator and native-title-bar work.

## Delivered behavior

- Minimal is the neutral default. Engineering, Office, Studio, Game and System
  families each offer light/dark, System mode and bounded customization.
- Window-owned controllers compile CSS only when its source changes, preserve
  widgets, accept layered application CSS and yield to explicit raw stylesheets.
- Windows appearance observations use owned snapshots and coalesced idle delivery;
  unsupported platforms and headless emulation remain explicit.
- The Creator palette paints category icons for visible rows, supports captured
  drag insertion into nested registered content slots, and commits one undo edit.
- Moving/resizing under Constraints shows container/sibling snap guides, with
  optional parent-relative grid and persistent ordinary solver anchors.
- Selected subtrees can become reusable session compositions with remapped IDs;
  independent instances persist through ordinary open JSON.
- Class export produces a complete editable .h/.cpp pair with owned construction,
  typed observer members and guidance for application hooks. Runtime JSON and the
  original direct-function C++ export remain available independently.

## Local verification

GCC 13 Release builds successfully. All **48/48 CTest suites pass**, including
actual GLFW null/OSMesa framebuffer tests and rendering-profile smoke checks.
The Theme suite has **59 assertions**: twelve presets, real style resolution,
unchanged-source reuse, coalesced preferences, contrast/motion rules, invalid
configuration rollback, override CSS, raw stylesheet handoff and recursive pump
rejection. The pure snapping fixture has **5 checks** for container/sibling axes,
resizing, a parent-relative grid and disabling the feature.

The Creator GPU probe exercises actual captured pointer drag/drop into a nested
GroupBox, one-step undo, reusable compositions/JSON, visible snap guides, persistent
parent anchors, all twelve appearance variants and widget identity retention.
Its previous inspector, real popup, runtime canvas, retained/full pixel equivalence
and window-chrome probes also continue to run. The generated catalog class header
and source are compiled and instantiated; every registered embeddable type is
present. The small theme API example in THEMES.md compiles against delivered
headers.

An early run exposed and corrected inactive Inspector ownership, raw geometry
fixtures unintentionally using new snapping, unwanted window padding, missing
primary-hover feedback and collection part rules overriding canonical explicit
selection colors. Final complete Release checks passed after these corrections.

MinGW syntax-checks the native appearance adapter's fallback path. Its SDK lacks
C++/WinRT and the native test's UI Automation headers, so this does not establish
MSVC or WinRT compatibility. Windows CI and sanitizer results will be recorded
against the published source once those runs complete.

## Performance and acceptance limits

The presets use existing GPU shapes/text and ordinary compiled CSS. Visible-row
palette icons are bounded paint records, not child widgets. Ordinary unchanged
frames add no OS appearance query, stylesheet parse or recurring theme timer.
The unchanged Creator repaint and retained/full framebuffer checks still pass.
Software GL proves rendering correctness, not hardware GPU load or frame rate.

Native appearance is implemented on Windows. Linux/macOS GLFW adapters currently
report Unsupported; they use deterministic fallback colors rather than guessed
OS preferences. Matching OS colors is not a promise of identical native controls.
Physical desktop appearance changes, mixed-DPI monitors, actual AT/high-contrast
use and hardware GPU measurements remain human/platform acceptance work.

Theme preferences and the 64-entry composition palette are session settings.
Composition instances are independent flattened subtrees, not linked instances.
Existing-widget reparenting, multi-selection, multiple authored content slots,
scoped subtree styles, persistent component catalogs and safe user-code merging
on regeneration remain future increments. C++ re-export deliberately replaces
the chosen source pair; retain custom additions before regeneration.
