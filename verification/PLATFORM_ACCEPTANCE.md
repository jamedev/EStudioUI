# Native platform acceptance

Run `./Build.ps1 -Configuration Release` and repeat with `-Configuration Debug`
on Windows. The normal script includes the real OpenGL framebuffer checks;
`-SkipGpuTest` is for hosts without a suitable OpenGL context. Hosted CI cannot
replace a developer-PC GPU, input-method, screen-reader, or mixed-monitor check.

For the opt-in test that changes the OS clipboard and opens/cancels native
pickers:

```powershell
$env:ESTUDIOUI_NATIVE_PLATFORM_TESTS = '1'
try {
    ./Build.ps1 -Configuration Release
    ./Build.ps1 -Configuration Debug
} finally {
    Remove-Item Env:ESTUDIOUI_NATIVE_PLATFORM_TESTS -ErrorAction SilentlyContinue
}
```

Record the tested commit, Windows version, input method, assistive technology,
GPU/driver, monitor scales, outcome, and reproduction steps. An unperformed check
is **not tested**, rather than a pass inferred from an automated contract test.

## Composition and focus

- Enter text using a Windows IME, change the active preedit, commit it, and undo
  once. Preedit must not appear as a committed document change or separate undo
  entries. Cancel with Escape and verify the original text and selection.
- Begin composition over selected text; commit and cancel in separate attempts.
  Test an invalid candidate in a field with a validator and a read-only field.
- Move/resize the window while composing, scroll the editor horizontally, and
  change its transformed position. Candidate placement should follow the caret.
- Move the window between monitors with different fractional scales. Check
  candidate placement, pointer selection, and reported accessibility bounds.
- Change focus, open/dismiss a popup, deactivate the window, and close the owner
  during composition. No stale candidate or late commit may edit a new target.
- Check ordinary keyboard text, dead keys, AltGr, shortcuts, and supplementary
  Unicode characters. A committed character must be inserted once.

This tests the platform composition protocol. Font fallback and complete
mixed-direction shaping retain their F10a acceptance requirements.

## Accessibility

- Start Narrator and navigate buttons, editable/read-only text, a numeric field,
  a slider, tabs, nested menus, and an owned window. Check names, roles, focus,
  enabled/read-only/toggle state, values, and canonical actions.
- Navigate list/tree/table entries beyond the visible viewport. Select and reveal
  an offscreen item; expand/collapse a tree branch; inspect table coordinates and
  headers where advertised. No per-row widgets should be created.
- Sort/filter/edit/delete model entries while the client retains an item. Stable
  model items must keep identity; removed items and replaced datasets must expire.
- Inspect a large canvas, reveal/select an item, then delete it. Check transformed
  and clipped geometry without enumerating the complete scene every frame.
- Listen for changes while renaming, editing, selecting, expanding, scrolling,
  hiding/showing, opening popups, and closing windows. Check that retained providers
  reject fresh actions after closure. Clients may retain copied property values.
- Repeat with a second assistive-technology client when available. Record its
  name/version separately; success with a direct UI Automation client is not
  evidence of a successful screen-reader session.

## Native services and lifetime

- Copy/paste supported text, HTML, file paths, and custom application formats;
  test a temporarily locked clipboard and unsupported format reporting.
- Open, save, and choose a folder. Cancel immediately and after the picker is
  visible. Keep another window interactive; close the owner during a picker.
- Drop files/folders onto the opt-in drop target, including Unicode paths.
- Close an owned window using both its button and native close control; reopen it
  and close the parent. Minimize/restore and check focus/cursor state.
- Repeat open/cancel/close cycles in Debug. Native resources must retire after
  asynchronous work unwinds without blocking the UI owner or losing completion.

## Recording results

Keep automated output and native observations in the release verification record.
Include a small reproduction or capture for a failure; do not replace renderer
output with a hand-made image. GPU timing requires named hardware and separate
CPU preparation/upload/GPU execution measurements.
