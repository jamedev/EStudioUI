#include "DemoScene.h"

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace EStudioUI
{
namespace
{
using Matrix = std::array<float, 16>;

Matrix Multiply(const Matrix& a, const Matrix& b)
{
    Matrix result{};
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                result[static_cast<std::size_t>(column * 4 + row)] +=
                    a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(column * 4 + k)];
    return result;
}

GLuint Shader(GLenum type, const char* source)
{
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled)
    {
        char log[4096]{};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Demo scene shader: ") + log);
    }
    return shader;
}
} // namespace

DemoScene::DemoScene(bool wireframe) : m_Wireframe(wireframe)
{
}

void DemoScene::Initialize()
{
    m_ContextIdentity = reinterpret_cast<std::uintptr_t>(glfwGetCurrentContext());
    const char* vertexSource = R"GLSL(#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aColor;
uniform mat4 uMvp;
out vec3 color;
void main() { gl_Position=uMvp*vec4(aPosition,1); color=aColor; }
)GLSL";
    const char* fragmentSource = R"GLSL(#version 330 core
in vec3 color;
out vec4 result;
void main() { result=vec4(color,1); }
)GLSL";
    const auto vertex = Shader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = 0;
    try
    {
        fragment = Shader(GL_FRAGMENT_SHADER, fragmentSource);
        m_Program = glCreateProgram();
        glAttachShader(m_Program, vertex);
        glAttachShader(m_Program, fragment);
        glLinkProgram(m_Program);
        GLint linked = 0;
        glGetProgramiv(m_Program, GL_LINK_STATUS, &linked);
        if (!linked)
            throw std::runtime_error("Demo scene program linking failed.");
    }
    catch (...)
    {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        throw;
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    const std::array<std::array<float, 3>, 8> corners{
        {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}};
    const std::array<std::array<int, 4>, 6> faces{
        {{4, 5, 6, 7}, {1, 0, 3, 2}, {0, 4, 7, 3}, {5, 1, 2, 6}, {3, 7, 6, 2}, {0, 1, 5, 4}}};
    const std::array<std::array<float, 3>, 6> colors{{{0.45f, 0.58f, 1},
                                                      {0.24f, 0.34f, 0.72f},
                                                      {0.2f, 0.65f, 0.65f},
                                                      {0.27f, 0.8f, 0.75f},
                                                      {0.65f, 0.74f, 1},
                                                      {0.25f, 0.35f, 0.55f}}};
    std::vector<float> vertices;
    const auto append = [&](const std::array<float, 3>& point, const std::array<float, 3>& color)
    {
        vertices.insert(vertices.end(), point.begin(), point.end());
        vertices.insert(vertices.end(), color.begin(), color.end());
    };
    for (std::size_t face = 0; face < faces.size(); ++face)
        for (int index : {0, 1, 2, 0, 2, 3})
            append(corners[static_cast<std::size_t>(faces[face][static_cast<std::size_t>(index)])], colors[face]);
    for (int line = -6; line <= 6; ++line)
    {
        const float coordinate = static_cast<float>(line);
        const std::array<float, 3> color =
            line == 0 ? std::array<float, 3>{0.3f, 0.5f, 0.65f} : std::array<float, 3>{0.13f, 0.21f, 0.3f};
        append({coordinate, -1.03f, -6}, color);
        append({coordinate, -1.03f, 6}, color);
        append({-6, -1.03f, coordinate}, color);
        append({6, -1.03f, coordinate}, color);
    }
    m_VertexCount = static_cast<int>(vertices.size() / 6);
    glGenVertexArrays(1, &m_Vao);
    glGenBuffers(1, &m_VertexBuffer);
    glBindVertexArray(m_Vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
}

void DemoScene::Render(const OpenGLFrame& frame)
{
    const float yaw = m_Yaw + (m_Animated ? static_cast<float>(std::fmod(frame.m_Time, 1000.0)) * 0.35f : 0);
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cx = std::cos(m_Pitch);
    const float sx = std::sin(m_Pitch);
    const Matrix rotationY{cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1};
    const Matrix rotationX{1, 0, 0, 0, 0, cx, sx, 0, 0, -sx, cx, 0, 0, 0, 0, 1};
    const Matrix translation{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -m_Distance, 1};
    const float aspect = static_cast<float>(frame.m_Width) / static_cast<float>(frame.m_Height);
    const float f = 1.0f / std::tan(0.8f * 0.5f);
    const Matrix projection{f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, -1.002002f, -1, 0, 0, -0.2002002f, 0};
    const auto mvp = Multiply(projection, Multiply(translation, Multiply(rotationX, rotationY)));
    glViewport(0, 0, frame.m_Width, frame.m_Height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClearColor(0.035f, m_Wireframe ? 0.075f : 0.055f, 0.095f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(m_Program);
    glUniformMatrix4fv(glGetUniformLocation(m_Program, "uMvp"), 1, GL_FALSE, mvp.data());
    glBindVertexArray(m_Vao);
    glPolygonMode(GL_FRONT_AND_BACK, m_Wireframe ? GL_LINE : GL_FILL);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDrawArrays(GL_LINES, 36, m_VertexCount - 36);
    ++m_RenderCount;
}

void DemoScene::Release()
{
    glDeleteProgram(m_Program);
    glDeleteBuffers(1, &m_VertexBuffer);
    glDeleteVertexArrays(1, &m_Vao);
    m_Program = m_VertexBuffer = m_Vao = 0;
}

void DemoScene::SetAnimated(bool animated)
{
    m_Animated = animated;
}

void DemoScene::ResetCamera()
{
    m_Yaw = 0.65f;
    m_Pitch = 0.4f;
    m_Distance = 5.5f;
}

void DemoScene::BeginDrag(Point point)
{
    m_LastPointer = point;
}

void DemoScene::Drag(Point point)
{
    m_Yaw += (point.m_X - m_LastPointer.m_X) * 0.01f;
    m_Pitch = std::clamp(m_Pitch + (point.m_Y - m_LastPointer.m_Y) * 0.01f, -1.3f, 1.3f);
    m_LastPointer = point;
}

void DemoScene::Zoom(float steps)
{
    m_Distance = std::clamp(m_Distance * std::pow(0.9f, steps), 2.8f, 16.0f);
}

std::uintptr_t DemoScene::GetContextIdentity() const
{
    return m_ContextIdentity;
}

unsigned DemoScene::GetRenderCount() const
{
    return m_RenderCount;
}
} // namespace EStudioUI
