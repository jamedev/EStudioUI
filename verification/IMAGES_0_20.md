# Image/icon resource verification (0.20)

Source implementation: `fbdf62d169a4d8231657702474985c9798bcee4a`.
W06 adds immutable shared Image values, bounded PNG/JPEG decoding, ImageView,
multi-density Icon values, command image slots, and Painter image drawing.
[IMAGE_RESOURCES.md](../IMAGE_RESOURCES.md) documents APIs, resource bounds,
owner/context lifetime, styling and remaining format limitations.

Local GNU 13 Release passes **40/40** suites in 17.61 seconds,
including **52** new resource/control checks and actual software-GL framebuffer
integration after source formatting. Local ASan/UBSan passes **40/40** in
93.46 seconds, including the GPU fixture (32.73 seconds) and profiling smoke
(9.35 seconds). Generated object/executable ELF headers were verified before
accepting that run. Three empty generated objects from an initial host build
were rebuilt; no product workaround or test relaxation was added. Leak detection
is disabled under host tracing and third-party native libraries are not instrumented.
Software rendering proves pixels and accounting; these timings do not establish
hardware GPU throughput.

[CI run 39943302584](https://github.com/jamedev/EStudioUI/actions/runs/39943302584)
passes Linux Release/software GL **40/40** in 16.50 seconds, including the image
and GPU fixtures, plus isolated CPU ThreadSanitizer **6/6** in 1.86 seconds.
There are no C++ compiler warnings. The existing libunibreak package emits three
CMake developer scope warnings during configuration; these are unchanged.

The same CI run passes MSVC 2022 **41/41 Release** (9.47 seconds) and
**41/41 Debug** (117.32 seconds). The native Win32/WGL framebuffer integration
passes with the isolated pinned Mesa 26.2.4 driver, including the new image
fixture and the PowerShell 5.1 rendering-profile smoke. Five extra native Debug
lifecycle runs each pass **97 checks**, including native image control-type and
keyboard-focus policy checks. There are no MSVC compiler warnings. This is
native-window/software-driver evidence; human AT and mixed-monitor acceptance
remain separate from these automated results.

The standalone [ImageWorkers probe](ImageWorkers.cpp) also passes ThreadSanitizer
on the actual Image.cpp/ImageDecoder.cpp sources: eight workers perform 800
successful decodes interleaved with truncated-input rejection, shared immutable
reads and unique-identity checks. It links no UI/native/GL/font library and does
not initialize a UI or GPU. This verifies the current CPU resource boundary;
F14 packaging/extraction remains deferred. Reproduce on a supported GCC/Linux
ThreadSanitizer host, with the pinned stb headers installed:

```sh
c++ -std=c++20 -O1 -g1 -fsanitize=thread -fno-omit-frame-pointer -pthread \
    -Iinclude -isystem build/linux-release/vcpkg_installed/x64-linux/include \
    src/Image.cpp src/ImageDecoder.cpp verification/ImageWorkers.cpp -o image-workers
TSAN_OPTIONS=halt_on_error=1 ./image-workers assets/images/pixel-fixture.png
```

The resource/control fixture covers copied immutable bytes and identities,
source density, dimensions/length/density failures, PNG transparency and row
orientation, JPEG alpha conversion, memory/file equivalence, truncated input,
eight concurrent decoding workers, density selection/validation, style parsing,
explicit size precedence, owner-thread checks, accessible Image semantics,
unchanged measurement caches and animated image positioning.

The actual renderer fixture covers:

- Repeated sources upload once per renderer, then upload zero bytes on unchanged
  frames. Warm retained images submit zero drawing work.
- PNG top/bottom/left/right orientation, one alpha blend, and filtering of an
  opaque red texel beside a fully transparent blue texel. The latter must not
  produce a blue fringe.
- Exact retained/full native-buffer comparisons while opacity changes among
  overlapping images/solid primitives, including clipped transformed images.
- Physical 2x drawing and density selection using deliberately different source
  colors; the expected source is observable in actual pixels.
- Explicit oversize-working-set rejection, subsequent-frame recovery, eviction
  and reload under a one-image budget, and correct retained identities after
  texture name reuse.
- Recorded ownership until deferred upload, weak expiry after the final CPU
  handle is released, and context-owner texture retirement.
- Caller unpack PBO/pixel-store isolation, and independent upload/use of the
  same CPU resource in a second Window context.
- Actual ImageView cover cropping/positioning, button/menu image slots, retained
  intrinsic measurements, and no static continuous-frame requests.

The Windows native semantic fixture additionally checks UIA_ImageControlTypeId
and the absence of an image keyboard-focus stop. Real assistive-technology and
mixed-monitor acceptance remain tracked in the existing platform checklist.

`image-gallery.ppm`, `image-widgets.ppm`, `image-primitives-2x.ppm` and
`image-report.txt` are produced by the C++ renderer/self-test. Artwork under
`assets/images/` is original procedural demonstration/test input, not substitute
UI screenshots. The compiled ImageResources example uses the actual task,
handle, Image and ImageView APIs.

[Saved actual widget pixels](image-widgets-0-20.png) are converted directly from
the renderer's PPM output (640 x 480); PNG SHA-256:
`53f2ede3a3c509b09bb42ccaa622666e2c2e48a8294be5a6516a9064ac4a8f79`.

SVG, animated images, ICC/EXIF normalization and the deferred document editor
are outside this milestone. F14 extraction remains deferred. W15 standalone
ScrollBar and W17 StackedWidget are the next catalog step.
