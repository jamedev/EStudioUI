#pragma once

#include "FontFace.h"
#include "Style.h"

#include <filesystem>
#include <memory>
#include <string>

namespace EStudioUI
{
//! Counters for the most recently submitted frame.
struct RenderStats
{
    //! Number of instanced draw submissions.
    std::size_t m_DrawCalls = 0;

    //! Number of shape instances.
    std::size_t m_Shapes = 0;

    //! Number of analytic glyph instances.
    std::size_t m_Glyphs = 0;

    //! Number of cached unique glyph outlines.
    std::size_t m_CachedGlyphs = 0;

    //! Bytes of outline and band data currently cached on the GPU.
    std::size_t m_FontBytes = 0;

    //! Bytes submitted in the ordered instance stream.
    std::size_t m_InstanceBytes = 0;

    //! Bytes submitted for transforms, clip chains, and analytic paint effects.
    std::size_t m_EffectBytes = 0;

    //! Number of analytic gradient fills.
    std::size_t m_Gradients = 0;

    //! Number of outer or inset shadow shapes.
    std::size_t m_Shadows = 0;
};

//! Batches analytic shapes and outline glyphs using an OpenGL 3.3 core context.
class Renderer
{
  public:
    //! Creates shaders and buffers in the current GL context.
    explicit Renderer(FontFace& font);

    //! Releases GL resources while their creating context is current.
    ~Renderer();

    //! GL resources have a single owner.
    Renderer(const Renderer&) = delete;

    //! GL resources have a single owner.
    Renderer& operator=(const Renderer&) = delete;

    //! Starts a new logical-pixel frame and clears the framebuffer.
    void BeginFrame(float width, float height, int framebufferWidth, int framebufferHeight, Color clear);

    //! Saves drawing state and composes a visual transform.
    void PushState(Affine local = {});

    //! Restores the preceding transform and clipping state.
    void PopState();

    //! Intersects the current clip with a local rounded rectangle.
    void PushClip(Rect bounds, float radius = 0);

    //! Supplies the owner and optional before layer for one widget paint call.
    void SetWidgetPaint(Rect bounds, Rect inheritedClip, const Style* before, bool rounded, float radius);

    //! Draws one noninteractive CSS pseudo-element.
    void DrawDecoration(Rect owner, const Style& style, Rect clip, float opacity);

    //! Appends a widget background, optional shadow, and border.
    void DrawBox(Rect bounds, const Style& style, Rect clip, float opacity = 1);

    //! Appends an unbordered solid rectangle.
    void DrawSolid(Rect bounds, Color color, Rect clip);

    //! Appends a cached shaped run at its baseline origin.
    void DrawText(const TextLayout& layout, Point baseline, Color color, Rect clip);

    //! Composites a shared RGBA GL texture, preserving surrounding painter order.
    void DrawTexture(Rect bounds, std::uint32_t texture, Rect clip, float opacity = 1);

    //! Uploads changed buffers and submits batches in painting order.
    void EndFrame();

    //! Reads a logical-pixel location from the current framebuffer.
    Color ReadPixel(Point point) const;

    //! Writes the current framebuffer as a portable binary PPM image.
    void SavePpm(const std::filesystem::path& path) const;

    //! Returns the last submitted frame's counters.
    const RenderStats& GetStats() const;

    //! Describes the current renderer and driver.
    std::string GetDriverDescription() const;

  private:
    //! Owns GL state, glyph buffers, and ordered frame batches.
    struct Impl;

    //! Keeps GL declarations outside public headers.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
