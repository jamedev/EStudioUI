# Images and icons (0.20)

`Image` is an immutable shared CPU resource. `ImageView`, command icons, and
custom `Painter` drawing use the existing retained GPU renderer. PNG and JPEG
decoding is supplied by the pinned header-only stb_image dependency under its
MIT license alternative; no new DLL or graphics backend is required.

## Ordinary use

```cpp
const auto picture = EStudioUI::Image::Load("drawing.png");
auto& preview = window.Ui().Root().Add<EStudioUI::ImageView>(picture);
preview.SetHeight(EStudioUI::SizeRule::Fill());
preview.SetAccessibleName("Drawing preview");
preview.SetFit("contain");

auto& inspect = window.Ui().Root().Add<EStudioUI::Button>("Inspect drawing");
inspect.SetImageIcon(EStudioUI::Image::Load("inspect.png"));
```

Copies share pixels and a lifetime-unique identity. `Image::FromRgba` copies
tightly packed, top-down, straight-alpha RGBA8 bytes. `Image::Decode` accepts
encoded bytes. `Load` accepts a `std::filesystem::path`, including native Windows
paths. Invalid data and limits produce exceptions before a resource is published.
The API does no network I/O, UI dispatch or OpenGL work during loading.

Loading/decoding can run in a worker. Publish the resulting `Image` through the
existing typed task/handle APIs and call `SetImage` on the UI owner. The compiled
[ImageResources example](examples/ImageResources.cpp) demonstrates a complete
receiver-bound load, including an error callback and window teardown. It accepts
a font path and an image path on the command line.

Each `ImageView` owns one shared source value. Replacing it with an equal natural
size is paint-only. Intrinsic size is physical source dimensions divided by the
source pixel scale; width-dependent measurement preserves that aspect ratio.
Explicit Fixed/Content/Fill sizing and hard limits retain their normal precedence.
An empty image is a valid empty control. Static image controls request no
continuous frames.

## CSS and drawing

```css
imageview {
    object-fit: cover;
    object-position: right center;
    padding: 8px;
    border: 1px solid #4a7295;
    border-radius: 16px;
    background: linear-gradient(120deg, #16334a, #102034);
    box-shadow: 0 5px 14px #0005;
}
button::image-icon, toolbutton::image-icon {
    width: 20px;
    height: 20px;
    gap: 8px;
}
button:disabled::image-icon { opacity: .4; }
```

`object-fit` accepts `contain`, `cover`, `fill`, `none` and `scale-down`.
`object-position` accepts one/two axis keywords or percentages in [0%,100%].
One horizontal keyword/percentage centers the other axis; `top`/`bottom` center
the horizontal axis. Two keyword axes may be reversed. The numeric computed
properties `ObjectPositionX` and `ObjectPositionY` are fractions in [0,1]; CSS
uses percentage spellings. `object-position-x/y` address an axis individually.
Position values interpolate through the existing CSS/property animation system;
fit changes are discrete. Neither property changes intrinsic measurement.

The source image is clipped to the view's content box and rounded border shape.
Root transforms, opacity, decorations and ancestor clipping use the shared
engine. `Painter::DrawImage(bounds, image, source)` and `DrawIcon(bounds, icon)`
respect the painter's transform, clipping and opacity. `source` is a normalized
top-left rectangle, defaulting to the whole image. No filesystem path is decoded
from painting or from a CSS animation tick.

The GPU upload premultiplies stored texture colors before linear filtering and
converts the filtered sample for the renderer's ordinary straight-alpha blending.
This avoids colored fringes around transparent pixels. Existing shared
OpenGLWidget textures preserve their existing orientation/alpha contract.
Pixels are displayed as supplied; ICC conversion, SVG and animated image formats
remain separate future adapters.

## Command icons and source density

```cpp
const EStudioUI::Icon inspect{
    EStudioUI::Image::Load("inspect.png"),
    EStudioUI::Image::Load("inspect-2x.png", 2)
};
button.SetImageIcon(inspect);
menu.AddAction("Inspect").SetImageIcon(inspect);
```

An `Icon` contains at most sixteen distinct source densities with equal natural
logical dimensions. The renderer selects the smallest adequate density for the
destination size, framebuffer scale and current transform, using the largest
source when all are smaller. Copying an icon copies shared handles, not pixels.

Buttons, ToolButtons, CheckBoxes/RadioButtons and MenuItems share image-icon
painting. Captions retain their existing wrapping/baseline behavior and image
slots follow caption direction. Image-only commands need `SetAccessibleName`.
The image is decorative within the command's existing semantic node and adds
no Tab stop. `ImageView` exposes the Image semantic role, mapped to the native
Windows UIA image control type, with the ordinary accessible-name setters.

`SetImageIconSize(Point)` gives application code precedence over CSS;
`ClearImageIconSize()` restores CSS. Default slots are 16 logical pixels with a
6-pixel caption gap; the bundled gallery stylesheet supplies its own sizes.
The part supports ordinary paint effects, opacity and transforms. Explicit icon
dimensions are limited to 1,024 logical pixels per axis.

MenuItem keeps its existing UTF-8 `SetIcon` API. A configured image takes priority
over that glyph fallback. Checked menu commands display the canonical check mark
in their leading slot. Shortcut hints, shared actions and submenu navigation
retain their existing behavior. Images are configured on each presentation;
they are not additional canonical Action state.

## Ownership and resource limits

| Resource | Bound/policy |
| --- | --- |
| Encoded input/file | 32 MiB |
| One decoded image | 64 MiB RGBA8; 16,384 pixels per axis |
| Decoder internal working allocations | 128 MiB per decoding thread; allocation failure becomes an exception |
| Icon sources | 16 distinct densities |
| Managed GPU textures | 64 MiB and 1,024 images per renderer by default |
| Configurable GPU budget | `Renderer::SetImageCacheLimit`, 4 bytes through 512 MiB |

Applications own their CPU images explicitly; there is no process-global image
cache accumulating file contents. The encoded input, decoded resource and
transient conversion buffers have separate lifetimes; the decoder working limit
does not represent a process-wide memory ceiling.

Textures belong to the renderer's creating context. They are uploaded once per
source residency, shared by repeated draws in that renderer, and independently
uploaded when used in another context. A frame retains its source pixels until
deferred upload completes. Cache entries hold weak CPU identities, and expired
sources retire at the next frame boundary. Live unused entries retire by least
recent frame when capacity is needed. Textures required by the current frame are
pinned; exceeding its working-set budget produces an explicit error. OpenGL
deletion happens on the context owner, including renderer teardown.

`ClearImageCache` and `SetImageCacheLimit` require a frame boundary and invalidate
retained history. Ordinary eviction/resource replacement preserves damage
tracking through actual texture handles and lifetime-unique source revisions.
Existing GL command ordering protects already-submitted texture uses; resources
are never destroyed by an arbitrary CPU image destructor.

`RenderStats` reports recorded `m_Images`, resident `m_CachedImages` and
`m_ImageBytes`, and per-frame `m_ImageUploadBytes`. Warm unchanged resources
upload zero bytes. These are logical RGBA8 payload/storage counts, excluding
driver allocation overhead. The **Images & icons** gallery exercises the public
controls; its verification uses actual C++ framebuffer pixels.
