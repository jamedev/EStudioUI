# EStudioUI development

- This repository is the source of truth. Use EStudioUI for project names,
  CMake targets, include directories, and the public C++ namespace.
- Use C++20. Keep each public class in a .h/.cpp pair; templates may be defined
  in headers. Headers use brief Doxygen //! comments for every method/member.
  Put detailed explanations in .cpp files only when the behavior needs them.
- Use English identifiers/comments, CamelCase methods, camelCase arguments and
  locals, m_CamelCase members, and s_CamelCase static members. Use Allman braces,
  blank lines between methods, and readable logical blocks.
- Keep the UI model/input code independent of GLFW/GL headers. Own widget children
  explicitly and preserve safe callback/focus/capture behavior on deletion.
- Add runtime dependencies through the pinned vcpkg manifest. Preserve the
  permissive-license constraint and keep optional FreeType/HarfBuzz features off
  unless a deliberate reviewed change requires them. Do not add Qt.
- On Windows, Build.ps1 installs x64-windows-estudioui dependencies, builds, runs
  core and GPU checks, and optionally opens the demo. -SkipGpuTest is explicit
  for hosts without OpenGL 3.3. Keep the script compatible with Windows PowerShell
  5.1 and PowerShell 7; do not modify a shared vcpkg checkout's branch or installed tree.
- For engine changes, run the core tests and appropriate real framebuffer checks.
  For build-script changes, check PowerShell parsing, normal Debug/Release flows,
  and fail-fast behavior. Report native/CI verification separately from mocks.
- Update CSS.md / ARCHITECTURE.md when the supported behavior changes. Captures
  and measurements must come from the actual C++ renderer. Do not substitute
  hand-made screenshots or infer hardware speed from a software GL run.
