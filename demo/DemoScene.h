#pragma once

#include <EStudioUI/OpenGLWidget.h>

namespace EStudioUI
{
//! A small depth-tested 3D scene used by the widget gallery and GPU checks.
class DemoScene
{
  public:
    //! Creates a colored or wireframe scene without allocating GPU resources.
    explicit DemoScene(bool wireframe = false);

    //! Creates context-local vertex arrays and scene shaders.
    void Initialize();

    //! Draws the cube and ground grid into the supplied widget framebuffer.
    void Render(const OpenGLFrame& frame);

    //! Releases resources while the scene's context is current.
    void Release();

    //! Enables or disables time-based rotation.
    void SetAnimated(bool animated);

    //! Resets orientation and camera distance.
    void ResetCamera();

    //! Starts an orbit gesture in window coordinates.
    void BeginDrag(Point point);

    //! Updates orbit orientation from a captured pointer move.
    void Drag(Point point);

    //! Changes the camera distance by wheel steps.
    void Zoom(float steps);

    //! Returns the context observed inside scene initialization.
    std::uintptr_t GetContextIdentity() const;

    //! Returns completed scene-render calls.
    unsigned GetRenderCount() const;

  private:
    //! Owns the scene shader program.
    unsigned m_Program = 0;

    //! Owns a context-local vertex array.
    unsigned m_Vao = 0;

    //! Owns cube and grid vertices.
    unsigned m_VertexBuffer = 0;

    //! Records the total vertex count.
    int m_VertexCount = 0;

    //! Selects wireframe display.
    bool m_Wireframe = false;

    //! Enables time-based rotation.
    bool m_Animated = false;

    //! Stores orbit yaw in radians.
    float m_Yaw = 0.65f;

    //! Stores orbit pitch in radians.
    float m_Pitch = 0.4f;

    //! Stores camera distance from the origin.
    float m_Distance = 5.5f;

    //! Stores the previous captured pointer position.
    Point m_LastPointer;

    //! Records initialization's actual native context.
    std::uintptr_t m_ContextIdentity = 0;

    //! Counts actual rendered frames.
    unsigned m_RenderCount = 0;
};
} // namespace EStudioUI
