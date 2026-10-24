# Search, feedback and remaining common controls (0.26)

Date: 2026-10-24. Source and CI identifiers are recorded in the evidence follow-up.

## Local acceptance

Final-source Release: **56/56 CTest suites passed**, 17.33 seconds. This includes
real OSMesa framebuffer integrations for the demo, Creator and SQLite browser,
plus the rendering-profile checks. Search/feedback reports **41 contract checks**;
common controls reports **33**. The production productivity gallery reports
**11 GPU/interaction checks**. The compiled search/picker examples and Creator's
direct C++/class exports also build.

The gallery verifies case-folded completion, pointer/keyboard acceptance, shared
Action execution, a notification coexisting with a modal palette, typed interval
bindings, retained/full pixel equality in light and dark, and zero repaint pixels
on unchanged static frames. Calendar and narrow-layout captures come from the
same C++ renderer. These software GL checks establish rendering correctness and
idle-work behavior; they do not measure the user's hardware GPU percentage.

The CPU suites cover timer ordering/cancellation/errors, ready-batch destruction,
receiver expiry, search coalescing and stale/focus/dismissal rejection, duplicate
result validation, owner deletion during publication, modal-safe feedback,
notification bounds, worker inbox overload/closure, immutable log identities,
tag binding rollback, interval crossing/separation, virtual semantic actions,
Gregorian leap/boundary rules, invalid civil drafts, local-time formats, font
validation/cancellation and application-routed links.

ASan/UBSan and native Windows/CI results are pending in this source commit and
will be recorded against the exact tested source. No hardware performance,
interactive IME or real screen-reader speech result is inferred from emulation.

## Scope

No runtime dependency was added. Text selection reuses the single-line read-only
engine; F10b documents and F14 module extraction remain separate. Font catalogs
are explicitly registered resources. Civil values have no implicit timezone or
locale conversion. Log retention is bounded for current storage; applications
retaining old immutable snapshots also retain their data. Rebuild cost is
explicitly linear per log batch, while painting stays viewport-bounded.

Creator registers all 15 new ordinary authorable controls and compiles their
exports. Complete specialized property/value adapters remain future authoring
work. Real Windows assistive-technology speech, physical pointer/IME behavior and
mixed-monitor/hardware performance keep the existing manual acceptance gates.
