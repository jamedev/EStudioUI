# Local verification / EStudioUI 0.2.0

These records come from the repository's actual C++ renderer and a real vcpkg
manifest install, not the previous source-bundled prototype. Runtime packages
were GLFW 3.4#1, FreeType 2.13.3, HarfBuzz 10.2.0#3, and utf8proc 2.10.0, all
without default features. Build-only vcpkg helper/Meson ports were also installed.

Debug and Release CTest runs passed both tests: 78 core outcome checks and the
GPU integration checks. `sanitizer-tests.txt` records the same checks with
AddressSanitizer/UndefinedBehaviorSanitizer on EStudioUI and its test/demo code.
Dependencies/Mesa were not instrumented. Leak detection was disabled because
this host's process tracing prevents LeakSanitizer; no leak-free claim is made.

`powershell-tests.txt` records 24 orchestration checks on PowerShell 7.6.7 with
mock native tools: Release/Debug, the custom triplet, the CRT flag, paths with
spaces, bootstrap, preserving nonempty directories, missing VS, CMake fallback,
and failure propagation at install/configure/build/core-test/GPU-test stages.
These mock checks do not establish native MSVC compilation. The GitHub Actions
workflow separately runs the real Build.ps1 under Windows PowerShell 5.1 for
MSVC Debug/Release, with -SkipGpuTest on hosted runners. Native GPU tests run by
default when a developer invokes Build.ps1 without that switch.

The PNGs are actual framebuffer captures: initial dark theme, focused selection,
light theme after editing, resized layout, a primitive scene at two times pixel
scale, and the 640-box/3,840-glyph stress scene. The driver was Mesa llvmpipe
software OpenGL. Benchmark samples include CPU submission and synchronous
readback and exclude widget layout/shaping; hardware throughput is unmeasured.
See benchmark.txt / benchmark.csv for this run's measurements.

The headless clipboard is an in-memory adapter. Native OS input, clipboard,
per-monitor DPI, and hardware drivers require target-machine validation.
