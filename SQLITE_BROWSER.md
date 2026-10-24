# EStudioUI Database Browser

A working application using the public data-view framework, not a separate set of
database-only widgets. On Windows, install dependencies, compile, test and launch:

```powershell
.\Build.ps1 -RunDatabaseBrowser
```

The ordinary one-shot script now checks the demo, Creator and database browser.
`-SkipGpuTest` remains the explicit option for hosts without OpenGL 3.3. A missing
executable or failed self-test fails the script. Run an existing file directly:

```powershell
.\build\windows-vcpkg\Release\EStudioUIDatabaseBrowser.exe --open C:\Data\engineering.sqlite
```

The application starts by creating a 100,000-row engineering example on a worker,
in the OS temporary directory's `EStudioUI` folder, if that file does not already
exist. It contains load cases, materials, assemblies and a read-only view. Creation
publishes a completed staging file without replacing an existing database. An
existing user database is never created implicitly and opens read-only. Check
**Enable editing** explicitly to allow addressable non-key cells to be changed.
Switching read-only/editable access retains the active table, filter, sort and page.
The Windows command line and file pickers preserve native Unicode paths.

- Choose a table/view from the left schema TreeTable. Expand it to inspect fields.
- Add typed filter conditions, combine them with AND/OR, and remove/clear them.
  Text predicates are literal; NULL tests do not use an empty-string convention.
- Click a column to sort. Shift-click adds secondary ordering. Headers reflect
  the last confirmed query, independently of visual column order.
- Drag headers or open **Columns...** to reorder/hide/show fields and fit widths.
  Use normal header separators to resize columns.
- First/Previous/Next/Last and 64/256/1024-row page sizes control bounded loading.
  Counts distinguish loaded, matching and total rows. Cancel loading keeps the
  previous coherent result. A committing write awaits its confirmed outcome.
- Double-click/F2 edits a cell with its typed control. Enter/Tab or the checkmark
  commits; Escape/cross cancels a draft. Exact optimistic checks and SQLite
  constraints prevent silent lost updates. Failed writes retain the draft.
- Export Page writes the current immutable page as CSV on a worker, including
  full exact binary values encoded as hexadecimal. It does not export unloaded rows.
- The second tab demonstrates ordinary C++ hierarchy models, shared selection,
  subtree moves, cross-view list drops, multi-key sorting and all typed editor kinds.
- Appearance switches use the existing theme system; the default is neutral light.

The scope is table/view browsing, filtering, sorting, paging and confirmed cell
updates. A SQL console, schema editor, table creation, record insertion/deletion,
imports, persistent ordering, arbitrary relationships and transaction history are
not implemented in this increment. The provider always opens a fresh connection
per operation and enables defensive mode, disables trusted-schema extensions,
uses a build without loadable extensions and enables foreign keys. These measures
are not a general sandbox for executing arbitrary third-party SQL extensions.

[DATA_VIEWS.md](DATA_VIEWS.md) defines extensibility, lifetime, resource and performance
contracts. SQLite is public domain; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Automated acceptance

The browser's `--headless --self-test --output folder` uses the actual C++ application,
SQLite adapter and renderer, with a dedicated generated test database. Windows CI
runs the visible native Win32/WGL path through a staged software OpenGL driver;
the Linux CTest integration uses OSMesa. Captures include ordinary/filtered SQL
results, captured tree-drop previews, typed editors and retained/full framebuffer
equality. Software GL proves correctness, not physical GPU throughput.

The title-bar regression additionally samples the **desktop** above the custom
caption, including title changes, inactive state and theme changes. This detects
a native caption painted over the client area, which client-rectangle assertions
alone did not detect. Native resize/shell hit testing remains independently tested.
Physical Windows 11 Snap flyouts, mixed-DPI monitors and real assistive technology
remain interactive platform acceptance items.


[Final-source verification and actual captures](verification/DATA_VIEWS_0_25.md)
record Windows Release/Debug 54/54 each, native browser 308 checks,
Linux 54/54, isolated TSan 6/6 and local ASan/UBSan 54/54.
