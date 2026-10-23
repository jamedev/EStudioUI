#pragma once

#include "FontFace.h"
#include "Style.h"
#include "ThreadAffinity.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace EStudioUI
{
//! Explains the most recent retained-frame repaint decision.
enum class RedrawReason
{
    //! No analytic pixels changed.
    None,
    //! Repaints only the damaged regions.
    Partial,
    //! The caller disabled partial redraw.
    FullRequested,
    //! No reusable previous framebuffer is available.
    HistoryInvalid,
    //! The framebuffer clear color changed.
    ClearChanged,
    //! The logical viewport changed.
    ViewportChanged,
    //! Damage exceeded the area budget.
    DamageArea,
    //! Damage exceeded the rectangle budget.
    DamageRectangles
};

//! Counters for the most recently submitted frame.
struct RenderStats
{
    //! Monotonic serial identifying this renderer's submitted frame.
    std::uint64_t m_FrameSerial = 0;

    //! Number of instanced draw submissions.
    std::size_t m_DrawCalls = 0;

    //! Number of recorded shape instances.
    std::size_t m_Shapes = 0;

    //! Number of recorded analytic glyph instances.
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

    //! Pixels cleared and analytically repainted in the retained framebuffer.
    std::size_t m_RepaintPixels = 0;

    //! Physical framebuffer area, also copied to the presentation target.
    std::size_t m_FramebufferPixels = 0;

    //! Actual instances submitted across damaged regions.
    std::size_t m_SubmittedInstances = 0;

    //! Actual analytic glyph instances submitted across damaged regions.
    std::size_t m_ReplayedGlyphs = 0;

    //! Nonoverlapping damage rectangles repainted this frame.
    std::size_t m_DamageRectangles = 0;

    //! Explains whether this frame reused pixels or selected a full redraw.
    RedrawReason m_RedrawReason = RedrawReason::None;

    //! Changed physical tiles before budget fallback; mandatory redraw reports the full target.
    std::size_t m_DirtyTileCount = 0;

    //! Changed physical tile area before budget fallback; mandatory redraw reports the full target.
    std::size_t m_DirtyTilePixels = 0;

    //! Rectangles accumulated when the damage planner made its decision.
    std::size_t m_DamageCandidateRectangles = 0;

    //! Most recent elapsed GPU timeline scope, or -1 before a result is available.
    double m_GpuMilliseconds = -1;

    //! Submitted frame serial associated with the asynchronous timing result.
    std::uint64_t m_GpuTimingFrame = 0;

    //! Timestamp counter precision; zero means unavailable or not yet queried.
    int m_GpuTimestampBits = 0;

    //! GPU time between upload-scope timestamps, or -1 before a completed result.
    double m_GpuUploadMilliseconds = -1;

    //! GPU time for retained clearing and drawing, or -1 before a completed result.
    double m_GpuDrawMilliseconds = -1;

    //! GPU time for the full presentation copy, excluding native swap/compositor work.
    double m_GpuPresentMilliseconds = -1;

    //! Completed asynchronous GPU timing samples.
    std::uint64_t m_TimingSamples = 0;
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

    //! Enables damage-based redraw; disabling provides a full redraw reference path.
    void SetPartialRedrawEnabled(bool enabled);

    //! Returns whether unchanged framebuffer regions are retained.
    bool IsPartialRedrawEnabled() const;

    //! Enables available asynchronous GPU timers without waiting for pending results.
    void SetGpuTimingEnabled(bool enabled);

    //! Retires glyph buffers between frames and invalidates retained framebuffer history.
    void ClearFontCache();

    //! Sets a hard GPU outline/band budget, retiring existing cached glyphs.
    void SetFontCacheLimit(std::size_t bytes);

    //! Starts recording a logical-pixel frame for retained rendering.
    void BeginFrame(float width, float height, int framebufferWidth, int framebufferHeight, Color clear);

    //! Saves drawing state and composes a visual transform.
    void PushState(Affine local = {});

    //! Restores the preceding transform and clipping state.
    void PopState();

    //! Intersects the current clip with a local rounded rectangle.
    void PushClip(Rect bounds, float radius = 0);

    //! Supplies the owner and optional before layer for one widget paint call.
    void SetWidgetPaint(Rect bounds, Rect inheritedClip, const Style* before, bool rounded, float radius,
                        std::uintptr_t ownerIdentity = 0);

    //! Assigns a stable item identity within an owner, isolated from ordinary widget paint keys.
    void SetPaintIdentity(std::uintptr_t ownerIdentity, std::uint64_t subIdentity = 0);

    //! Draws a CSS pseudo-element in layer 1 (before) or 2 (after).
    void DrawDecoration(Rect owner, const Style& style, Rect clip, float opacity, unsigned layer = 1);

    //! Appends a widget background, optional shadow, and border.
    void DrawBox(Rect bounds, const Style& style, Rect clip, float opacity = 1);

    //! Appends an unbordered solid rectangle.
    void DrawSolid(Rect bounds, Color color, Rect clip);

    //! Appends an analytic round-capped line with width in local logical pixels.
    void DrawLine(Point from, Point to, Color color, float width, Rect clip);

    //! Appends an analytic circle with CSS fill, border, gradient, and shadows.
    void DrawCircle(Point center, float radius, const Style& style, Rect clip, float opacity = 1);

    //! Appends a cached shaped run at its baseline origin.
    void DrawText(const TextLayout& layout, Point baseline, Color color, Rect clip);

    //! Composites a shared texture; revision zero conservatively treats its contents as changed.
    void DrawTexture(Rect bounds, std::uint32_t texture, Rect clip, float opacity = 1, std::uint64_t revision = 0);

    //! Uploads changed buffers and submits batches in painting order.
    void EndFrame();

    //! Reads the last completed GPU frame with this renderer's context current, independently of native swaps.
    Color ReadPixel(Point point) const;

    //! Writes the last completed GPU frame as PPM with this renderer's context current.
    void SavePpm(const std::filesystem::path& path) const;

    //! Returns the last submitted frame's counters.
    const RenderStats& GetStats() const;

    //! Describes the current renderer and driver.
    std::string GetDriverDescription() const;

  private:
    //! Enforces access before any renderer state or OpenGL command is touched.
    ThreadAffinity m_Affinity;

    //! Owns GL state, glyph buffers, and ordered frame batches.
    struct Impl;

    //! Keeps GL declarations outside public headers.
    std::unique_ptr<Impl> m_Impl;
};
} // namespace EStudioUI
