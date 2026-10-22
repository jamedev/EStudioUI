#pragma once

#include "PainterPath.h"
#include "Renderer.h"

#include <span>
#include <string_view>
#include <vector>

namespace EStudioUI
{
//! A scoped immediate drawing facade that records commands into the existing GPU batch.
class Painter
{
  public:
    //! Saves renderer state, installs a parent-space clip, and translates to a local origin.
    Painter(Renderer& renderer, FontFace& font, Rect clip, float opacity = 1, Point origin = {});

    //! Restores all renderer state, including unbalanced local Save calls.
    ~Painter();

    //! A painter exclusively owns its scoped renderer states.
    Painter(const Painter&) = delete;

    //! A painter exclusively owns its scoped renderer states.
    Painter& operator=(const Painter&) = delete;

    //! Saves pen, brush, opacity, transform, and clipping state.
    void Save();

    //! Restores the latest Save; throws when there is no matching Save.
    void Restore();

    //! Composes a finite, invertible local transform.
    void Transform(const Affine& transform);

    //! Translates the current local coordinate system.
    void Translate(float x, float y);

    //! Scales coordinates and stroke widths; zero scales are rejected.
    void Scale(float x, float y);

    //! Rotates local coordinates clockwise in radians around their origin.
    void Rotate(float radians);

    //! Intersects the current clip with a local rectangle, optionally rounded.
    void ClipRect(Rect bounds, float radius = 0);

    //! Sets straight-alpha stroke/text color and nonnegative local stroke width.
    void SetPen(Color color, float width = 1);

    //! Sets an untextured fill and clears previous brush effects.
    void SetBrush(Color color);

    //! Sets typed CSS fill/gradient/shadow paint; pen settings supply the border.
    void SetBrush(const Style& style);

    //! Sets local opacity multiplied by the constructor's inherited opacity.
    void SetOpacity(float opacity);

    //! Draws an analytic round-capped line using the current pen.
    void DrawLine(Point from, Point to);

    //! Draws a rectangle using the current brush and pen.
    void DrawRect(Rect bounds);

    //! Draws a uniformly rounded rectangle using the current brush and pen.
    void DrawRoundedRect(Rect bounds, float radius);

    //! Draws an analytic circle using the current brush and pen.
    void DrawCircle(Point center, float radius);

    //! Strokes finite vertices using round-capped segments; does not fill the interior.
    void DrawPolyline(std::span<const Point> points, bool closed = false);

    //! Strokes a cached adaptive path with a 0.35-pixel target adjusted for painter transforms.
    void DrawPath(const PainterPath& path);

    //! Draws a pre-shaped text run at its local baseline, avoiding shaping work during paint.
    void DrawText(const TextLayout& layout, Point baseline);

    //! Shapes and draws UTF-8 text; reuse a TextLayout for frequently repainted content.
    void DrawText(std::string_view text, Point baseline, float fontSize = 16);

    //! Returns the font service for application-owned shaping caches.
    FontFace& GetFont() const;

  private:
    //! Keeps scoped renderer and drawing state on the constructing thread.
    ThreadAffinity m_Affinity;

    //! CPU presentation paired with renderer state.
    struct State
    {
        //! Current pen color.
        Color m_Pen{1, 1, 1, 1};

        //! Current local stroke width.
        float m_Width = 1;

        //! Fill, gradient, and shadow settings.
        Style m_Brush;

        //! Local opacity independent of the inherited opacity.
        float m_Opacity = 1;

        //! Local-to-constructor-parent transform.
        Affine m_Transform;

        //! Renderer stack depth immediately before the associated Save.
        std::size_t m_Depth = 0;
    };

    //! Returns a conservative local rectangle for the fixed outer clipping region.
    Rect DrawingClip() const;

    //! Combines the current brush and pen into one analytic shape presentation.
    Style ShapeStyle() const;

    //! Observes the renderer only for the painter's lexical lifetime.
    Renderer& m_Renderer;

    //! Observes the font service.
    FontFace& m_Font;

    //! Fixed outer rectangle in constructor-parent coordinates.
    Rect m_Clip;

    //! Ancestor opacity supplied by the containing widget.
    float m_InheritedOpacity = 1;

    //! Current CPU presentation.
    State m_State;

    //! Saved CPU presentations corresponding to renderer Save states.
    std::vector<State> m_Saved;

    //! Number of renderer states owned by this painter.
    std::size_t m_Depth = 0;
};
} // namespace EStudioUI
