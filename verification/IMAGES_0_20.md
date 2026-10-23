# Image/icon resource verification (0.20)

W06 adds immutable shared Image values, bounded PNG/JPEG decoding, ImageView,
multi-density Icon values, command image slots, and Painter image drawing.
[IMAGE_RESOURCES.md](../IMAGE_RESOURCES.md) documents APIs, resource bounds,
owner/context lifetime, styling and remaining format limitations.

Local GNU 13 Release passes **40/40** suites in 17.61 seconds,
including **52** new resource/control checks and actual software-GL framebuffer
integration after source formatting. Native and sanitizer verification will be
recorded after their checks finish. Software rendering proves pixels and
accounting; these timings do not establish hardware GPU throughput.

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

SVG, animated images, ICC/EXIF normalization and the deferred document editor
are outside this milestone. F14 extraction remains deferred. W15 standalone
ScrollBar and W17 StackedWidget are the next catalog step.
