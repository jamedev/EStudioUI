# Embedded OpenGL scenes

`OpenGLWidget` owns a separate OpenGL 3.3 core context in a hidden GLFW window.
It shares GPU objects with its containing window, renders into an RGBA8 texture
with a depth/stencil renderbuffer, and composites that texture at the widget's
position in the UI painter order. No framebuffer readback or CPU image copy is
used by this production path. The ordinary UI widgets remain GPU drawn.

GLFW associates a context with a window; hidden windows and explicit framebuffers
are its supported offscreen pattern. See the primary
[GLFW context guide](https://www.glfw.org/docs/3.4/context_guide.html).

## Scene lifecycle

Include `EStudioUI/OpenGLWidget.h`. Public library headers do not include GLFW,
GLAD, or operating-system headers. Use a GL loader in application scene code; the
demo explicitly links `EStudioUIGl` and uses the bundled generated GLAD header.
Its complete depth-tested cube/grid implementation is in
[`demo/DemoScene.h`](demo/DemoScene.h) and
[`demo/DemoScene.cpp`](demo/DemoScene.cpp).

```cpp
#include <EStudioUI/OpenGLWidget.h>
#include <glad/gl.h>

void AddSurface(EStudioUI::Widget& parent)
{
    using namespace EStudioUI;
    auto& surface = parent.Add<OpenGLWidget>();
    surface.SetWidth(SizeRule::Fill(1, 200));
    surface.SetHeight(SizeRule::Fill(1, 160));
    surface.SetCallbacks(
        [] { /* Create your shaders, buffers, and context-local VAO here. */ },
        [](const OpenGLFrame& frame)
        {
            // The widget's framebuffer and physical-size viewport are already bound.
            glViewport(0, 0, frame.m_Width, frame.m_Height);
            glClearColor(0.03f, 0.05f, 0.09f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            // Submit scene geometry here. See DemoScene for a complete 3D example.
        },
        [] { /* Delete scene resources here, with their context current. */ });
}
```

The initialization callback runs on the first visible scene frame. Rendering
runs after initialization and when a surface is invalidated or resized. Replacing
callbacks releases the previous initialized scene in its context. Partially
initialized scenes also receive cleanup when replaced/destroyed. Destructors
catch cleanup exceptions; normal initialization/render exceptions propagate to
the caller after restoring the containing window's context.

`OpenGLFrame` provides physical framebuffer width/height, the widget-owned FBO
name, and monotonic GUI time. The color/depth attachments are allocated to the
widget's content size times the current per-axis framebuffer scale. Resize/DPI
changes reallocate attachments in the same context. Exceeding the driver's
texture-size limit is diagnosed. Do not delete widget-owned attachments.

## Interaction and frame requests

Connect `PointerPressed`, `PointerDragged`, `PointerReleased`, `Scrolled`, and
`KeyInput` for scene input. Events use logical window coordinates, so subtract
`GetContentBounds().m_X/m_Y` when local scene coordinates are needed. Pointer
capture continues outside the viewport. The gallery connects drag to orbit and
wheel to camera distance independently for its two scenes.

Call `RequestFrame()` on the GUI thread when camera/model data changes. Static
scenes reuse their textures while other UI controls repaint. Enable
`SetContinuousRendering(true)` for animation; the window paces animation at up to
60 requested frames/second, subject to vertical sync and rendering time. It
accounts for draw/swap time instead of adding a second full-frame sleep. Hidden
or empty surfaces do not keep the event loop animating.

A separate context is **not a separate render thread**. All scene lifecycle and
input callbacks currently run on the GUI thread. Do not destroy/reparent widgets
or replace scene callbacks inside scene initialization/render/cleanup callbacks;
change scene lifecycle from ordinary input/model callbacks. Worker-thread
rendering and queued signal delivery have not been implemented.

## Sharing and isolation

Texture objects are shared; FBOs and VAOs are created in each scene context.
A producer fence and flush order UI sampling after scene rendering. A consumer
fence orders the next scene update/resize/cleanup after preceding UI sampling.
The CPU does not call `glFinish` to coordinate these operations. Program, blend,
depth, framebuffer, and vertex-array state in a scene context cannot overwrite
the UI context's state. Leave the scene's context current when a callback returns.

`Window::RenderFrame` performs preparation, UI drawing, and consumer fencing.
Applications manually driving `UiContext`/`Renderer` must keep the owning UI
context current and use this sequence:

1. `ui.Prepare(logicalWidth, logicalHeight)`.
2. `ui.PrepareSurfaces(time, scaleX, scaleY)`.
3. `renderer.BeginFrame(...)`, `ui.Paint(renderer, time)`, `renderer.EndFrame()`.
4. `ui.FrameSubmitted()`.

An embedded texture splits the otherwise unified shape/glyph batch where
necessary to preserve painter order. Scene drawing has its own submissions.
The no-surface UI and existing stress benchmark still use one ordered draw.

The GPU integration test uses actual separate contexts, verifies distinct texture
colors and scene state isolation, checks physical-size/depth attachments,
resizing, static scene caching, exceptions, recovery, and cleanup. It also renders
two interactive 3D cube/grid scenes. Headless checks use Mesa software OpenGL;
hardware performance and platform-specific driver behavior still need target
machine validation. No new runtime dependencies were added for this control.
