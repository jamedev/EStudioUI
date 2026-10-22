#include <EStudioUI/OpenGLWidget.h>
#include <EStudioUI/Renderer.h>

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cmath>
#include <exception>
#include <stdexcept>

namespace EStudioUI
{
namespace
{
// GUI-thread content generations remain distinct even when GL reuses a deleted
// texture name and a replacement widget occupies the same address.
std::uint64_t s_TextureRevision = 0;

//! Restores the caller's context even when scene initialization/rendering throws.
class ContextScope
{
  public:
    explicit ContextScope(GLFWwindow* context) : m_Previous(glfwGetCurrentContext())
    {
        glfwMakeContextCurrent(context);
    }

    ~ContextScope()
    {
        glfwMakeContextCurrent(m_Previous);
    }

  private:
    GLFWwindow* m_Previous;
};
} // namespace

struct OpenGLWidget::Impl
{
    GLFWwindow* m_Context = nullptr;
    GLFWwindow* m_Owner = nullptr;
    GLuint m_Framebuffer = 0;
    GLuint m_Texture = 0;
    std::uint64_t m_Revision = 0;
    GLuint m_Depth = 0;
    GLsync m_Consumer = nullptr;
    int m_Width = 0;
    int m_Height = 0;
    bool m_Initialized = false;
    bool m_Dirty = true;
    bool m_Continuous = false;
    bool m_Dragging = false;
    std::function<void()> m_Initialize;
    std::function<void(const OpenGLFrame&)> m_Render;
    std::function<void()> m_Release;
    Signal<PointerEvent> m_Pressed;
    Signal<PointerEvent> m_Dragged;
    Signal<PointerEvent> m_Released;
    Signal<Point> m_Scrolled;
    Signal<KeyboardEvent> m_Keys;

    ~Impl()
    {
        if (!m_Context)
            return;
        {
            ContextScope scope(m_Context);
            if (m_Consumer)
            {
                glWaitSync(m_Consumer, 0, GL_TIMEOUT_IGNORED);
                glDeleteSync(m_Consumer);
            }
            if (m_Initialized && m_Release)
            {
                try
                {
                    m_Release();
                }
                catch (...)
                { /* Destructors cannot propagate scene cleanup failures. */
                }
            }
            glDeleteFramebuffers(1, &m_Framebuffer);
            glDeleteRenderbuffers(1, &m_Depth);
            glDeleteTextures(1, &m_Texture);
        }
        if (glfwGetCurrentContext() == m_Context)
            glfwMakeContextCurrent(m_Owner);
        glfwDestroyWindow(m_Context);
    }

    void CreateContext()
    {
        m_Owner = glfwGetCurrentContext();
        if (!m_Owner)
            throw std::runtime_error("OpenGLWidget requires the containing window's context to be current.");
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, glfwGetWindowAttrib(m_Owner, GLFW_CONTEXT_CREATION_API));
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
        m_Context = glfwCreateWindow(1, 1, "EStudioUI scene context", nullptr, m_Owner);
        glfwDefaultWindowHints();
        if (!m_Context)
            throw std::runtime_error("Could not create a shared OpenGLWidget context.");
    }

    void Resize(int width, int height)
    {
        if (!m_Framebuffer)
        {
            glGenFramebuffers(1, &m_Framebuffer);
            glGenTextures(1, &m_Texture);
            glGenRenderbuffers(1, &m_Depth);
        }
        glBindTexture(GL_TEXTURE_2D, m_Texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindRenderbuffer(GL_RENDERBUFFER, m_Depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_Texture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_Depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("OpenGLWidget framebuffer is incomplete.");
        m_Width = width;
        m_Height = height;
        m_Dirty = true;
    }
};

OpenGLWidget::OpenGLWidget() : Widget("openglwidget"), m_Impl(std::make_unique<Impl>())
{
}

OpenGLWidget::~OpenGLWidget()
{
    if (!IsOwnerThread())
        std::terminate();
}

void OpenGLWidget::SetCallbacks(std::function<void()> initialize, std::function<void(const OpenGLFrame&)> render,
                                std::function<void()> release)
{
    VerifyAccess();
    const auto ownerLease = RetainOwner();
    auto& self = *m_Impl;
    if (self.m_Context && self.m_Initialized)
    {
        ContextScope scope(self.m_Context);
        if (self.m_Consumer)
        {
            glWaitSync(self.m_Consumer, 0, GL_TIMEOUT_IGNORED);
            glDeleteSync(self.m_Consumer);
            self.m_Consumer = nullptr;
        }
        self.m_Initialized = false;
        if (self.m_Release)
            self.m_Release();
    }
    self.m_Initialize = std::move(initialize);
    self.m_Render = std::move(render);
    self.m_Release = std::move(release);
    RequestFrame();
}

void OpenGLWidget::SetContinuousRendering(bool enabled)
{
    VerifyAccess();
    m_Impl->m_Continuous = enabled;
    RequestFrame();
}

void OpenGLWidget::RequestFrame()
{
    VerifyAccess();
    m_Impl->m_Dirty = true;
    RequestRepaint();
}

Point OpenGLWidget::GetFramebufferSize() const
{
    VerifyAccess();
    return {static_cast<float>(m_Impl->m_Width), static_cast<float>(m_Impl->m_Height)};
}

std::uintptr_t OpenGLWidget::GetContextIdentity() const
{
    VerifyAccess();
    return reinterpret_cast<std::uintptr_t>(m_Impl->m_Context);
}

Signal<PointerEvent>& OpenGLWidget::PointerPressed()
{
    VerifyAccess();
    return m_Impl->m_Pressed;
}

Signal<PointerEvent>& OpenGLWidget::PointerDragged()
{
    VerifyAccess();
    return m_Impl->m_Dragged;
}

Signal<PointerEvent>& OpenGLWidget::PointerReleased()
{
    VerifyAccess();
    return m_Impl->m_Released;
}

Signal<Point>& OpenGLWidget::Scrolled()
{
    VerifyAccess();
    return m_Impl->m_Scrolled;
}

Signal<KeyboardEvent>& OpenGLWidget::KeyInput()
{
    VerifyAccess();
    return m_Impl->m_Keys;
}

bool OpenGLWidget::IsFocusable() const
{
    VerifyAccess();
    return true;
}

Point OpenGLWidget::Measure(FontFace&) const
{
    return {320, 220};
}

void OpenGLWidget::PrepareFrame(double time, float scaleX, float scaleY)
{
    const auto bounds = GetContentBounds();
    if (bounds.m_Width <= 0 || bounds.m_Height <= 0)
        return;
    auto& self = *m_Impl;
    if (!self.m_Context)
        self.CreateContext();
    GLsync completed = nullptr;
    {
        ContextScope scope(self.m_Context);
        if (self.m_Consumer)
        {
            glWaitSync(self.m_Consumer, 0, GL_TIMEOUT_IGNORED);
            glDeleteSync(self.m_Consumer);
            self.m_Consumer = nullptr;
        }
        GLint maximum = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
        const double physicalWidth = std::ceil(static_cast<double>(bounds.m_Width) * scaleX);
        const double physicalHeight = std::ceil(static_cast<double>(bounds.m_Height) * scaleY);
        if (physicalWidth > maximum || physicalHeight > maximum || physicalWidth < 1 || physicalHeight < 1)
            throw std::runtime_error("OpenGLWidget size exceeds the current driver's texture limit.");
        const int width = static_cast<int>(physicalWidth);
        const int height = static_cast<int>(physicalHeight);
        if (width != self.m_Width || height != self.m_Height)
            self.Resize(width, height);
        if (!self.m_Dirty && !self.m_Continuous)
            return;
        self.m_Dirty = false;
        glBindFramebuffer(GL_FRAMEBUFFER, self.m_Framebuffer);
        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.035f, 0.055f, 0.09f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (!self.m_Initialized)
        {
            // Mark before invoking user code so partial initialization is also cleaned up.
            self.m_Initialized = true;
            if (self.m_Initialize)
                self.m_Initialize();
        }
        if (glfwGetCurrentContext() != self.m_Context)
            throw std::runtime_error("Scene initialization must leave the OpenGLWidget context current.");
        // Initialization may create other render targets. Restore the promised
        // scene destination and physical viewport before each render callback.
        glBindFramebuffer(GL_FRAMEBUFFER, self.m_Framebuffer);
        glViewport(0, 0, width, height);
        if (self.m_Render)
            self.m_Render({width, height, self.m_Framebuffer, time});
        if (glfwGetCurrentContext() != self.m_Context)
            throw std::runtime_error("Scene callbacks must leave the OpenGLWidget context current.");
        if (glGetError() != GL_NO_ERROR)
            throw std::runtime_error("An OpenGLWidget scene callback produced an OpenGL error.");
        self.m_Revision = ++s_TextureRevision;
        completed = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
    }
    // The shared texture remains on the GPU. Order the consumer after the scene
    // commands without blocking the CPU with glFinish or framebuffer readback.
    if (completed)
    {
        glWaitSync(completed, 0, GL_TIMEOUT_IGNORED);
        glDeleteSync(completed);
    }
}

void OpenGLWidget::Paint(Renderer& renderer, FontFace& font, double time, float opacity) const
{
    Widget::Paint(renderer, font, time, opacity);
    renderer.DrawTexture(GetContentBounds(), m_Impl->m_Texture, GetContentBounds().Intersect(GetClip()), opacity,
                         m_Impl->m_Revision);
}

bool OpenGLWidget::NeedsContinuousRendering() const
{
    return GetContentBounds().m_Width > 0 && GetContentBounds().m_Height > 0 &&
           (m_Impl->m_Continuous || m_Impl->m_Dirty);
}

void OpenGLWidget::FrameSubmitted()
{
    if (!m_Impl->m_Texture || GetContentBounds().m_Width <= 0 || GetContentBounds().m_Height <= 0)
        return;
    if (m_Impl->m_Consumer)
        glDeleteSync(m_Impl->m_Consumer);
    m_Impl->m_Consumer = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
}

void OpenGLWidget::OnPointerDown(const PointerEvent& event)
{
    m_Impl->m_Dragging = true;
    m_Impl->m_Pressed.Emit(event);
}

void OpenGLWidget::OnPointerMove(const PointerEvent& event)
{
    if (m_Impl->m_Dragging)
        m_Impl->m_Dragged.Emit(event);
}

void OpenGLWidget::OnPointerUp(const PointerEvent& event)
{
    m_Impl->m_Dragging = false;
    m_Impl->m_Released.Emit(event);
}

void OpenGLWidget::OnCaptureLost()
{
    m_Impl->m_Dragging = false;
}

bool OpenGLWidget::OnScroll(Point delta, Modifiers)
{
    if (!m_Impl->m_Scrolled.HasConnections())
        return false;
    m_Impl->m_Scrolled.Emit(delta);
    return true;
}

void OpenGLWidget::OnKey(Key key, bool down, bool repeat, Modifiers modifiers)
{
    m_Impl->m_Keys.Emit({key, down, repeat, modifiers});
}
} // namespace EStudioUI
